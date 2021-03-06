#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#ifdef WIN32
#define __STDC_FORMAT_MACROS			// Enable integer types
#endif
#include <inttypes.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "lcmgen.h"


#define INDENT(n) (4*(n))

#define emit_start(n, ...) do { fprintf(f, "%*s", INDENT(n), ""); fprintf(f, __VA_ARGS__); } while (0)
#define emit_continue(...) do { fprintf(f, __VA_ARGS__); } while (0)
#define emit_end(...) do { fprintf(f, __VA_ARGS__); fprintf(f, "\n"); } while (0)
#define emit(n, ...) do { fprintf(f, "%*s", INDENT(n), ""); fprintf(f, __VA_ARGS__); fprintf(f, "\n"); } while (0)

#if 0
static char *dots_to_slashes(const char *s)
{
    char *p = strdup(s);

    for (char *t=p; *t!=0; t++)
        if (*t == '.')
            *t = G_DIR_SEPARATOR;

    return p;
}

static void make_dirs_for_file(const char *path)
{
#ifdef WIN32
    char *dirname = g_path_get_dirname(path);
    g_mkdir_with_parents(dirname, 0755);
    g_free(dirname);
#else
    int len = strlen(path);
    for (int i = 0; i < len; i++) {
        if (path[i]=='/') {
            char *dirpath = (char *) malloc(i+1);
            strncpy(dirpath, path, i);
            dirpath[i]=0;

            mkdir(dirpath, 0755);
            free(dirpath);

            i++; // skip the '/'
        }
    }
#endif
}
#endif

void setup_vala_options(getopt_t *gopt)
{
    getopt_add_string (gopt, 0, "vala-path",    ".",      "Location for .vala files");
}

static const char *map_type_name (const char* t)
{
    if      (!strcmp ("byte", t))    return "int8";
    else if (!strcmp ("boolean", t)) return "bool";
    else if (!strcmp ("int8_t", t))  return "int8";
    else if (!strcmp ("int16_t", t)) return "int16";
    else if (!strcmp ("int32_t", t)) return "int32";
    else if (!strcmp ("int64_t", t)) return "int64";
    //else if (!strcmp ("float", t))   return "float";
    //else if (!strcmp ("double", t))  return "double";
    //else if (!strcmp ("string", t))  return "string";

    return t;
}

static const char *make_dynarray_type(const char *buf, size_t maxlen, char *type, unsigned int ndim)
{
    FILE *f = fmemopen(buf, maxlen, "w");

    fprintf(f, "%s[", type);
    for (unsigned int d = 1; d < ndim; d++)
        fputc(',', f);
    fputc(']', f);

    fclose(f);
    return buf;
}

static const char *make_dynarray_accessor(const char *buf, size_t maxlen, char *membername, unsigned int n_ax)
{
    FILE *f = fmemopen(buf, maxlen, "w");

    fprintf(f, "this.%s", membername);
    if (n_ax > 0)
        fputc('[', f);
    for (int i = 0; i < n_ax; i++)
        fprintf(f, "a%d%s", i, (i + 1 < n_ax)? ", " : "");
    if (n_ax > 0)
        fputc(']', f);

    fclose(f);
    return buf;
}

static int is_dim_size_fixed(const char* dim_size) {
    char *eptr = NULL;
    long asdf = strtol(dim_size, &eptr, 0);
    (void) asdf;  // suppress compiler warnings
    return (*eptr == '\0');
}

static const char * dim_size_prefix(const char *dim_size) {
    if (is_dim_size_fixed(dim_size))
        return "";
    else
        return "this.";
}

static void emit_auto_generated_warning(FILE *f)
{
    fprintf(f,
            "/* THIS IS AN AUTOMATICALLY GENERATED FILE.\n"
            " * DO NOT MODIFY BY HAND!!\n"
            " *\n"
            " * Generated by lcm-gen\n"
            " */\n\n");
}

static void emit_comment(FILE* f, int indent, const char* comment) {
    if (!comment)
        return;

    gchar** lines = g_strsplit(comment, "\n", 0);
    int num_lines = 0;
    for (num_lines = 0; lines[num_lines]; num_lines++) {}

    if (num_lines == 1) {
        emit(indent, "//! %s", lines[0]);
    } else {
        emit(indent, "/**");
        for (int line_ind = 0; lines[line_ind]; line_ind++) {
            if (strlen(lines[line_ind])) {
                emit(indent, " * %s", lines[line_ind]);
            } else {
                emit(indent, " *");
            }
        }
        emit(indent, " */");
    }
    g_strfreev(lines);
}

static void emit_class_start(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    const char *tn = ls->structname->lctypename;

    emit_comment(f, 0, ls->comment);
    emit(0, "public class %s : Lcm.IMessage, Object {", tn);
}

static void emit_class_end(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    emit(0, "}");
}

static void emit_data_members(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    if (g_ptr_array_size(ls->members) == 0) {
        emit(1, "// no data members");
        emit(0, "");
        return;
    }

    emit(1, "// data members begin");
    for (unsigned int mind = 0; mind < g_ptr_array_size(ls->members); mind++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, mind);

        emit_comment(f, 1, lm->comment);
        const char* mapped_typename = map_type_name(lm->type->lctypename);
        int ndim = g_ptr_array_size(lm->dimensions);
        if (ndim == 0) {
            emit(1, "public %-10s %s;", mapped_typename, lm->membername);
        } else {
            // vala only supports one dimension fixed size array,
            // so for simplifying all arrays now dynamic
            char buf[256];
            emit(1, "public %-10s %s;",
                    make_dynarray_type(buf, sizeof(buf), mapped_typename, ndim),
                    lm->membername);
        }
    }
    emit(1, "// end");
    emit(0, "");
}

static void emit_const_members(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    if (g_ptr_array_size(ls->constants) == 0) {
        emit(1, "// no constants");
        emit(0, "");
        return;
    }

    emit(1, "// constants begin");
    for (unsigned int i = 0; i < g_ptr_array_size(ls->constants); i++) {
        lcm_constant_t *lc = (lcm_constant_t *) g_ptr_array_index(ls->constants, i);
        assert(lcm_is_legal_const_type(lc->lctypename));

        const char* mapped_typename = map_type_name(lc->lctypename);

        emit_comment(f, 1, lc->comment);
        emit(1, "public const %-10s %s = (%s) %s;",
                mapped_typename, lc->membername,
                mapped_typename, lc->val_str);
    }
    emit(1, "// end");
    emit(0, "");
}

// check that member is string or user defined message
// or fixed array
static int is_message_or_const_array(lcm_member_t *lm)
{
    int ndim = g_ptr_array_size(lm->dimensions);

    return
        !lcm_is_primitive_type(lm->type->lctypename) ||
        (ndim > 0 && lcm_is_constant_size_array(lm));
}

static void emit_constructor(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    // constructor needed only for user defined messages
    // and to preallocate constant size arrays
    int constructor_needed = FALSE;
    for (unsigned int mind = 0; mind < g_ptr_array_size(ls->members); mind++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, mind);

        if (is_message_or_const_array(lm)) {
            constructor_needed = TRUE;
            break;
        }
    }

    if (!constructor_needed) {
        emit(1, "// no special constructor needed");
        emit(0, "");
        return;
    }

    emit(1, "//! Constructor.");
    emit(1, "public %s() {", ls->structname->shortname);

    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, m);
        int ndim = g_ptr_array_size(lm->dimensions);

        // construct complex types and const array only.
        if (!is_message_or_const_array(lm)) {
            emit(2,     "// '%s' initialized by %s", lm->membername, (ndim > 0)? "user" : "class construction");
            continue;
        }

        emit_start(2, "this.%s", lm->membername);

        if (ndim > 0) {
            emit_continue(" = new %s[", map_type_name(lm->type->lctypename));
            for (int i = 0; i < ndim; i++) {
                lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, i);
                emit_continue("%s%s%s", (i > 0)? ", " : "", dim_size_prefix(dim->size), dim->size);
            }
            emit_end("];");

            // allocate message types only
            if (lcm_is_primitive_type(lm->type->lctypename))
                continue;

            for (int n = 0; n < ndim; n++) {
                lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, n);
                emit(2 + n, "for (int a%d = 0; a%d < %s%s; a%d++) {",
                        n, n, dim_size_prefix(dim->size), dim->size, n);
            }

            emit_start(2 + ndim, "this.%s[", lm->membername);
            for (int i = 0; i < ndim; i++)
                emit_continue("%sa%d", (i > 0)? ", " : "", i);
            emit_end("] = new %s();", lm->type->lctypename);

            for (int n = ndim - 1; n >= 0; n--)
                emit(2 + n, "}");

        } else {
            emit_end(" = new %s();", lm->type->lctypename);
        }
    }

    emit(1, "}");
    emit(0, "");
}

static void emit_constructor_from_rbuf(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    emit(1, "//! Construct and decode message from Lcm.RecvBuf");
    emit(1, "public %s.from_rbuf(Lcm.RecvBuf rbuf) throws Lcm.MessageError {", ls->structname->shortname);
    emit(2,     "this.decode(rbuf.data);");
    emit(1, "}");
    emit(0, "");
}

static void emit_encode(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    emit(1, "public void[] encode() throws Lcm.MessageError {");
    emit(2,     "Posix.off_t pos = 0;");
    emit(2,     "int64 hash_ = this.hash;");
    emit(2,     "var buf = new void[Lcm.CoreTypes.int64_SIZE + this._encoded_size_no_hash];");
    emit(0, "");
    emit(2,     "pos += Lcm.CoreTypes.int64_encode_array(buf, pos, &hash_, 1);");
    emit(2,     "this._encode_no_hash(buf, pos);");
    emit(0, "");
    emit(2,     "return buf;");
    emit(1, "}");
    emit(0, "");
}

static void emit_decode(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    emit(1, "public void decode(void[] data) throws Lcm.MessageError {");
    emit(2,     "Posix.off_t pos = 0;");
    emit(2,     "int64 hash_ = 0;");
    emit(0, "");
    emit(2,     "pos += Lcm.CoreTypes.int64_decode_array(data, pos, &hash_, 1);");
    emit(2,     "if (hash_ != this.hash)");
    emit(3,         "throw new Lcm.MessageError.WRONG_HASH(\"%s\");", ls->structname->lctypename);
    emit(0, "");
    emit(2,     "this._decode_no_hash(data, pos);");
    emit(1, "}");
    emit(0, "");
}

static void vala_encode_recursive(lcmgen_t* lcm, FILE* f, lcm_member_t* lm, int depth, int extra_indent)
{
    int indent = extra_indent + 2 + depth;
    int ndim = g_ptr_array_size(lm->dimensions);

    // primitive array
    if (depth + 1 == ndim &&
            lcm_is_primitive_type(lm->type->lctypename) &&
            strcmp(lm->type->lctypename, "string") != 0) {

        lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, depth);

        emit_start(indent, "pos += Lcm.CoreTypes.%s_encode_array(data, offset + pos, &this.%s[",
                map_type_name(lm->type->lctypename), lm->membername);
        for (int i = 0; i < depth; i++)
            emit_continue("a%d, ", i);
        emit_end("0], %s%s);", dim_size_prefix(dim->size), dim->size);

        return;
    }
    // recursion end
    if (depth == ndim) {
        char accessor[256];
        make_dynarray_accessor(accessor, sizeof(accessor), lm->membername, depth);

        if (strcmp(lm->type->lctypename, "string") == 0) {
            emit(indent, "unowned string _temp_%s = %s;", lm->membername, accessor);
            emit(indent, "pos += Lcm.CoreTypes.string_encode(data, offset + pos, _temp_%s);", lm->membername);
        } else {
            emit(indent, "pos += %s._encode_no_hash(data, offset + pos);", accessor);
        }
        return;
    }

    lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, depth);

    emit(indent, "for (int a%d = 0; a%d < %s%s; a%d++) {",
            depth, depth, dim_size_prefix(dim->size), dim->size, depth);

    vala_encode_recursive(lcm, f, lm, depth + 1, extra_indent);

    emit(indent, "}");
}

static void emit_encode_nohash(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
	emit(1, "public size_t _encode_no_hash(void[] data, Posix.off_t offset) throws Lcm.MessageError {");
    if (0 == g_ptr_array_size(ls->members)) {
        emit(2,     "return 0;");
        emit(1, "}");
        emit(0, "");
        return;
    }

    emit(2, "size_t pos = 0;");
    emit(0, "");
    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, m);
        int num_dims = g_ptr_array_size(lm->dimensions);

        if (0 == num_dims) {
            if (lcm_is_primitive_type(lm->type->lctypename)) {
                if (strcmp(lm->type->lctypename, "string") == 0) {
                    emit(2, "pos += Lcm.CoreTypes.string_encode(data, offset + pos, this.%s);",
                            lm->membername);
                } else {
                    emit(2, "pos += Lcm.CoreTypes.%s_encode_array(data, offset + pos, &this.%s, 1);",
                            map_type_name(lm->type->lctypename), lm->membername);
                }
            } else {
                vala_encode_recursive(lcm, f, lm, 0, 0);
            }
        } else {
            lcm_dimension_t *last_dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, num_dims - 1);

            // for non-string primitive types with variable size final
            // dimension, add an optimization to only call the primitive encode
            // functions only if the final dimension size is non-zero.
            if (lcm_is_primitive_type(lm->type->lctypename) &&
                    strcmp(lm->type->lctypename, "string") != 0 &&
                    !is_dim_size_fixed(last_dim->size)) {
                emit(2, "if (%s%s > 0) {", dim_size_prefix(last_dim->size), last_dim->size);
                vala_encode_recursive(lcm, f, lm, 0, 1);
                emit(2, "}");
            } else {
                vala_encode_recursive(lcm, f, lm, 0, 0);
            }
        }

        emit(0, "");
    }
    emit(2,     "return pos;");
    emit(1, "}");
    emit(0, "");
}

static void vala_decode_recursive(lcmgen_t* lcm, FILE* f, lcm_member_t* lm, int depth)
{
    int indent = 2 + depth;
    int ndim = g_ptr_array_size(lm->dimensions);

    // primitive array
    if (depth + 1 == ndim &&
        lcm_is_primitive_type(lm->type->lctypename) &&
        strcmp(lm->type->lctypename, "string") != 0) {

        lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, depth);

        emit_start(indent, "pos += Lcm.CoreTypes.%s_decode_array(data, offset + pos, &this.%s[",
                map_type_name(lm->type->lctypename), lm->membername);
        for (int i = 0; i < depth; i++)
            emit_continue("a%d, ", i);
        emit_end("0], %s%s);", dim_size_prefix(dim->size), dim->size);

        return;
    }
    // recursion end
    if (depth == ndim) {
        char accessor[256];
        make_dynarray_accessor(accessor, sizeof(accessor), lm->membername, depth);

        if (strcmp(lm->type->lctypename, "string") == 0) {
            emit(indent, "pos += Lcm.CoreTypes.string_decode(data, offset + pos, out %s);", accessor);
        } else {
            emit(indent, "if (%s == null) %s = new %s();", accessor, accessor, lm->type->lctypename);
            emit(indent, "pos += %s._decode_no_hash(data, offset + pos);", accessor);
        }

        return;
    }

    lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, depth);

    emit(indent, "for (int a%d = 0; a%d < %s%s; a%d++) {",
            depth, depth, dim_size_prefix(dim->size), dim->size, depth);

    vala_decode_recursive(lcm, f, lm, depth + 1);

    emit(indent, "}");
}

static void emit_decode_nohash(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
	emit(1, "public size_t _decode_no_hash(void[] data, Posix.off_t offset) throws Lcm.MessageError {");
    if (0 == g_ptr_array_size(ls->members)) {
        emit(2,     "return 0;");
        emit(1, "}");
        emit(0, "");
        return;
    }

    emit(2,     "size_t pos = 0;");
    emit(0, "");
    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, m);

        if (0 == g_ptr_array_size(lm->dimensions) && lcm_is_primitive_type(lm->type->lctypename)) {
            if (strcmp(lm->type->lctypename, "string") == 0) {
                emit(2, "pos += Lcm.CoreTypes.string_decode(data, offset + pos, out this.%s);",
                        lm->membername);
            } else {
                emit(2, "pos += Lcm.CoreTypes.%s_decode_array(data, offset + pos, &this.%s, 1);",
                        map_type_name(lm->type->lctypename), lm->membername);
            }
        } else {
            int ndim = g_ptr_array_size(lm->dimensions);

            // for dynamic arrays emit reallocate code
            if (0 != ndim && !lcm_is_constant_size_array(lm)) {
                emit_start(2, "if (");
                for (int i = 0; i < ndim; i++) {
                    char len_idx[256] = "";
                    lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, i);

                    if (ndim > 1)
                        snprintf(len_idx, sizeof(len_idx), "[%d]", i);

                    emit_continue("this.%s.length%s != %s%s",
                            lm->membername, len_idx, dim_size_prefix(dim->size), dim->size);
                    if (i + 1 < ndim)
                        emit_continue(" || ");
                }
                emit_end(") {");

                emit_start(3, "this.%s = new %s[", lm->membername, map_type_name(lm->type->lctypename));
                for (int i = 0; i < ndim; i++) {
                    lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, i);
                    emit_continue("%s%s", dim_size_prefix(dim->size), dim->size);
                    if (i + 1 < ndim)
                        emit_continue(", ");
                }
                emit_end("];");

                emit(2, "}");
            }

            vala_decode_recursive(lcm, f, lm, 0);
        }

        emit(0, "");
    }

	emit(2,     "return pos;");
	emit(1, "}");
    emit(0, "");
}

static void emit_encoded_size_nohash(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    emit(1, "public size_t _encoded_size_no_hash {");
    emit(2,     "get {");

    if (0 == g_ptr_array_size(ls->members)) {
        emit(3,     "return 0;");
        emit(2, "}");
        emit(1,"}");
        emit(0,"");
        return;
    }

    emit(3,     "size_t enc_size = 0;");
    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, m);
        int ndim = g_ptr_array_size(lm->dimensions);
        int is_string = strcmp(lm->type->lctypename, "string") == 0;
        const char* mapped_typename = map_type_name(lm->type->lctypename);

        if (lcm_is_primitive_type(lm->type->lctypename) && !is_string) {
            emit_start(3, "enc_size += ");
            for (int n = 0; n < ndim - 1; n++) {
                lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, n);
                emit_continue("%s%s * ", dim_size_prefix(dim->size), dim->size);
            }
            if (ndim > 0) {
                lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, ndim - 1);
                emit_end("Lcm.CoreTypes.%s_SIZE * %s%s;",
                        mapped_typename, dim_size_prefix(dim->size), dim->size);
            } else {
                emit_end("Lcm.CoreTypes.%s_SIZE;", mapped_typename);
            }
        } else {
            for (int n = 0; n < ndim; n++) {
                lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, n);
                emit(3 + n, "for (int a%d = 0; a%d < %s%s; a%d++) {",
                        n, n, dim_size_prefix(dim->size), dim->size, n);
            }

            // use unowned for eliminate strdup/object ref/unref
            // but vala does not allow `unowned var` so we explicitly define type.
            emit_start(3 + ndim, "unowned %s _temp_%s = this.%s",
                    map_type_name(lm->type->lctypename), lm->membername, lm->membername);
            if (ndim > 0) {
                emit_continue("[");
                for (int i = 0; i < ndim; i++)
                    emit_continue("%sa%d", (i > 0)? ", " : "", i);
                emit_continue("]");
            }
            emit_end(";");

            if (is_string) {
                // strings may be empty, return empty size (length + terminator)
                emit(3 + ndim, "enc_size += _temp_%s != null ? "
                        "_temp_%s.length + Lcm.CoreTypes.int32_SIZE + 1 : "
                        "Lcm.CoreTypes.int32_SIZE + 1;",
                        lm->membername, lm->membername);
            } else {
                // object must be constructed
                emit(3 + ndim, "assert(_temp_%s != null);", lm->membername);
                emit(3 + ndim, "enc_size += _temp_%s._encoded_size_no_hash;", lm->membername);
            }

            for (int n = ndim - 1; n >= 0; n--)
                emit(3 + n, "}");
        }
    }
    emit(3,         "return enc_size;");
    emit(2,     "}");
    emit(1, "}");
    emit(0, "");
}


static void emit_hash_param(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    emit(1, "private static int64 _hash = _compute_hash(null);");
	emit(1,	"public int64 hash {");
	emit(2,     "get { return _hash; }");
	emit(1,	"}");
    emit(0, "");
}

static void emit_compute_hash(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    int last_complex_member = -1;
    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, m);
        if (!lcm_is_primitive_type(lm->type->lctypename))
            last_complex_member = m;
    }

    emit(1, "public static int64 _compute_hash(Lcm.CoreTypes.intptr[]? parents) {");
    emit(2,     "if (((Lcm.CoreTypes.intptr) _compute_hash) in parents)");
    emit(3,         "return 0;");
    emit(0, "");
    if (last_complex_member >= 0) {
        emit(2,     "Lcm.CoreTypes.intptr[] cp = parents;");
        emit(2,     "cp += ((Lcm.CoreTypes.intptr) _compute_hash);");
        emit(0, "");
        emit(2,     "int64 hash_ = 0x%016"PRIx64" +", ls->hash);

        for (unsigned int m = 0; m <= last_complex_member; m++) {
            lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, m);

            if (!lcm_is_primitive_type(lm->type->lctypename)) {
                emit(3, " %s._compute_hash(cp)%s",
                        lm->type->lctypename,
                        (m == last_complex_member) ? ";" : " +");
            }
        }
    } else {
        emit(2,     "int64 hash_ = 0x%016"PRIx64";", ls->hash);
    }
    emit(0, "");
    emit(2,     "return (hash_ << 1) + ((hash_ >> 63) & 1);");
    emit(1, "}");
}

int emit_vala(lcmgen_t *lcmgen)
{
    // iterate through all defined message types
    for (unsigned int i = 0; i < g_ptr_array_size(lcmgen->structs); i++) {
        lcm_struct_t *lr = (lcm_struct_t *) g_ptr_array_index(lcmgen->structs, i);

        const char *tn = lr->structname->lctypename;

        // compute the target filename
        char *file_name = g_strdup_printf("%s%s%s.vala",
                getopt_get_string(lcmgen->gopt, "vala-path"),
                strlen(getopt_get_string(lcmgen->gopt, "vala-path")) > 0 ? G_DIR_SEPARATOR_S : "",
                tn);

        // generate code if needed
        if (lcm_needs_generation(lcmgen, lr->lcmfile, file_name)) {
            //make_dirs_for_file(file_name);

            FILE *f = fopen(file_name, "w");
            if (f == NULL)
                return -1;

            emit_auto_generated_warning(f);
            emit_class_start(lcmgen, f, lr);

            emit_data_members(lcmgen, f, lr);
            emit_const_members(lcmgen, f, lr);

            emit_constructor(lcmgen, f, lr);
            emit_constructor_from_rbuf(lcmgen, f, lr);

            emit_encode(lcmgen, f, lr);
            emit_decode(lcmgen, f, lr);

            emit_encode_nohash(lcmgen, f, lr);
            emit_decode_nohash(lcmgen, f, lr);
            emit_encoded_size_nohash(lcmgen, f, lr);

            emit_hash_param(lcmgen, f, lr);
            emit_compute_hash(lcmgen, f, lr);
            emit_class_end(lcmgen, f, lr);

            fclose(f);
        }

        g_free(file_name);
    }

    return 0;

}
