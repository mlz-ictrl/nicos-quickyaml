/*******************************************************************************
 * YAML dumper for NICOS, the Networked Instrument Control System of the FRM-II
 * Copyright (c) 2016-2023 by the NICOS contributors (see AUTHORS)
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Module authors:
 *   Georg Brandl <g.brandl@fz-juelich.de>
 *
 ******************************************************************************/

#include <Python.h>
#include <structmember.h>
#include <numpy/arrayobject.h>

#define BYTES_FMT "y"
#define TO_UNICODE PyObject_Str
#define TO_INT PyNumber_Long

#define bool  int
#define TRUE  1
#define FALSE 0

#define ARRAY_AS_SEQ 0
#define ARRAY_AS_MAP 1

#define UNICODE_LS 0x2028
#define UNICODE_PS 0x2029
#define UNICODE_NONCHAR_RESERVED 0xfffe
#define CHR_DELETE 0x7f
#define CHR_APP_PROGRAM_CMD 0x9f


/******************************************************************************/
/* flowlist object */

/* This is an empty list subclass, the equivalent of

   class flowlist(list):
       pass

   It is used to signal to the dumper to use flow representation
   for the list, as opposed to block representation.
*/

static PyTypeObject flowlist_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "quickyaml.flowlist",
    sizeof(PyListObject),
    0,
    0,                                  /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_compare */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    0,                                  /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT
        | Py_TPFLAGS_BASETYPE,          /* tp_flags */
    0,                                  /* tp_doc */
    0,                                  /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    0,                                  /* tp_iter */
    0,                                  /* tp_iternext */
    0,                                  /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    0,                                  /* tp_new */
    0                                   /* tp_free */
};

/******************************************************************************/
/* Helpers */

static char *HEX = "0123456789abcdef";

/* Determine if the given Unicode string needs quoting and escaping when
   represented as a YAML scalar.

   There are several factors that determine this:

   * Empty strings must always be quoted
   * Control characters must always be escaped
   * Some characters are unsafe anywhere in a bare string
     (e.g. the comma, as it separates elements in flow style)
   * Some characters are unsafe at the beginning of a string
     (e.g. the bang, which introduces tags, or digits, which
     introduce numbers)
   * Some characters are unsafe at the end (whitespace)
   * Some strings indicate other data types (booleans or nulls)

   In general, this function errs on the safe side and might quote
   a few too many strings, which only slightly influences the
   human readability and does not impact loading.
*/
static bool
needs_quoting(PyObject *string, Py_ssize_t len)
{
    Py_ssize_t i;
    Py_UCS4 buf[5];
    if (len == 0)
        return TRUE;
    for (i = 0; i < len; i++) {
        Py_UCS4 ch = PyUnicode_ReadChar(string, i);
        /* Characters disallowed everywhere: */
        if (ch < 32 ||
            ch == '"' || ch == '\\' || ch == ',' || ch == '?' ||
            ch == '[' || ch == ']' || ch == '{' || ch == '}' ||
            ch == ':' || ch == '#' || ch == UNICODE_LS || ch == UNICODE_PS ||
            (ch >= CHR_DELETE && ch <= CHR_APP_PROGRAM_CMD) ||
            ch >= UNICODE_NONCHAR_RESERVED)
            return TRUE;
        /* Characters disallowed at the beginning: */
        if (i == 0 && (ch == '&' || ch == '*' || ch == '!' ||
                       ch == '|' || ch == '>' || ch == '\'' ||
                       ch == '%' || ch == '@' || ch == '`' ||
                       ch == '-' || ch == '+' || ch == ' ' ||
                       ch == '=' || ch == '~' ||
                       (ch >= '0' && ch <= '9')))
            return TRUE;
        /* Characters disallowed at the end: */
        if (i == len - 1 && ch == ' ')
            return TRUE;
    }
    /* Words that would be null or booleans: */
    if (len == 2) {
        PyUnicode_AsUCS4(string, buf, 5, FALSE);
        if (tolower(buf[0]) == 'o' &&
            tolower(buf[1]) == 'n')
            return TRUE;
        if (tolower(buf[0]) == 'n' &&
            tolower(buf[1]) == 'o')
            return TRUE;
    } else if (len == 3) {
        PyUnicode_AsUCS4(string, buf, 5, FALSE);
        if (tolower(buf[0]) == 'y' &&
            tolower(buf[1]) == 'e' &&
            tolower(buf[2]) == 's')
            return TRUE;
        if (tolower(buf[0]) == 'o' &&
            tolower(buf[1]) == 'f' &&
            tolower(buf[2]) == 'f')
            return TRUE;
    } else if (len == 4) {
        PyUnicode_AsUCS4(string, buf, 5, FALSE);
        if (tolower(buf[0]) == 't' &&
            tolower(buf[1]) == 'r' &&
            tolower(buf[2]) == 'u' &&
            tolower(buf[3]) == 'e')
            return TRUE;
        if (tolower(buf[0]) == 'n' &&
            tolower(buf[1]) == 'u' &&
            tolower(buf[2]) == 'l' &&
            tolower(buf[3]) == 'l')
            return TRUE;
    } else if (len == 5) {
        PyUnicode_AsUCS4(string, buf, 5, FALSE);
        if (tolower(buf[0]) == 'f' &&
            tolower(buf[1]) == 'a' &&
            tolower(buf[2]) == 'l' &&
            tolower(buf[3]) == 's' &&
            tolower(buf[4]) == 'e')
            return TRUE;
    }
    return FALSE;
}

/* Quote and escape the given string (given by content and length) into a new
   string object.
 */
static PyObject *
quote_string(PyObject *string, Py_ssize_t len)
{
    Py_ssize_t i = 0;
    Py_ssize_t size = 2 * len + 3;
    Py_UCS4 *dst, *buf_end;
    Py_UCS4 *buffer = PyMem_Malloc(sizeof(Py_UCS4) * size);
    if (!buffer)
        return NULL;
    buf_end = buffer + size;
    dst = buffer;
    *dst++ = '"';
    for (i = 0; i < len; i++) {
        if (buf_end - dst < 11) {
            /* Need to reallocate. */
            Py_UCS4 *new_buffer;
            Py_ssize_t offset = dst - buffer;
            size *= 2;
            new_buffer = PyMem_Realloc(buffer, sizeof(Py_UCS4) * size);
            if (!new_buffer) {
                PyMem_Free(buffer);
                return NULL;
            }
            buffer = new_buffer;
            buf_end = buffer + size;
            dst = buffer + offset;
        }
        Py_UCS4 ch = PyUnicode_ReadChar(string, i);
        if (ch == '\\') {
            *dst++ = '\\';
            *dst++ = '\\';
        } else if (ch == '"') {
            *dst++ = '\\';
            *dst++ = '"';
        } else if (ch == '\n') {
            *dst++ = '\\';
            *dst++ = 'n';
        } else if (ch == '\t') {
            *dst++ = '\\';
            *dst++ = 't';
        } else if (ch < 32 || (ch >= CHR_DELETE && ch <= CHR_APP_PROGRAM_CMD)) {
            *dst++ = '\\';
            *dst++ = 'x';
            *dst++ = HEX[(ch & 0xf0) >> 4];
            *dst++ = HEX[ch & 0x0f];
        } else if (ch == UNICODE_LS || ch == UNICODE_PS ||
                   ch >= UNICODE_NONCHAR_RESERVED) {
            *dst++ = '\\';
            *dst++ = 'U';
            *dst++ = HEX[(ch & 0xf0000000) >> 28];
            *dst++ = HEX[(ch & 0x0f000000) >> 24];
            *dst++ = HEX[(ch & 0x00f00000) >> 20];
            *dst++ = HEX[(ch & 0x000f0000) >> 16];
            *dst++ = HEX[(ch & 0x0000f000) >> 12];
            *dst++ = HEX[(ch & 0x00000f00) >> 8];
            *dst++ = HEX[(ch & 0x000000f0) >> 4];
            *dst++ = HEX[ch & 0x0000000f];
        } else {
            *dst++ = ch;
        }
    }
    *dst++ = '"';
    return PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, buffer, dst - buffer);
}

/* Write a single literal string to the output. */
static bool
write_literal(PyObject *write, const char *str)
{
    PyObject *result = PyObject_CallFunction(write, BYTES_FMT, str);
    Py_XDECREF(result);
    return !!result;
}

/* Write a single bytestring object to the output. */
static bool
write_object(PyObject *write, PyObject *str)
{
    PyObject *result = PyObject_CallFunctionObjArgs(write, str, NULL);
    Py_XDECREF(result);
    return !!result;
}

/* Make a string with n spaces for indentation. */
static void
make_indent(char *buffer, int n)
{
    int j;
    for (j = 0; j < n; ++j)
        *buffer++ = ' ';
    *buffer = '\0';
}

/******************************************************************************/
/* Dumper functions */

/* This stores local settings during the dumping process. */
typedef struct {
    int array_handling;
    int indentwidth;
    int maxwidth;
    int curindent;
    int curcol;
    PyObject *callback;
} dumpdata;

static bool dispatch_dump(PyObject *, PyObject *, dumpdata *, bool, bool);

/* Here we define functions for dumping each supported type. */

static bool
dump_none(PyObject *write, dumpdata *data)
{
    data->curcol += 4;
    return write_literal(write, "null");
}

static bool
dump_bool(PyObject *obj, PyObject *write, dumpdata *data)
{
    data->curcol += obj == Py_True ? 4 : 5;
    return write_literal(write, obj == Py_True ? "true" : "false");
}

static bool
dump_int(PyObject *obj, PyObject *write, dumpdata *data)
{
    bool success = FALSE;
    /* Easy: format the integer as a unicode string, and write it out. */
    PyObject *converted = TO_UNICODE(obj);
    PyObject *encoded = NULL;
    if (!converted)
        return FALSE;
    if (!(encoded = PyUnicode_AsUTF8String(converted)))
        goto exit;
    success = write_object(write, encoded);
    data->curcol += PyBytes_Size(encoded);
  exit:
    Py_XDECREF(encoded);
    Py_DECREF(converted);
    return success;
}

static bool
dump_float(PyObject *obj, PyObject *write, dumpdata *data)
{
    bool success = FALSE;
    PyObject *converted = NULL;
    PyObject *encoded = NULL;
    /* Not as easy: special floats are represented slightly differently in YAML,
       so catch them beforehand. */
    double val = PyFloat_AsDouble(obj);
    if (Py_IS_NAN(val)) {
        data->curcol += 4;
        return write_literal(write, ".nan");
    } else if (Py_IS_INFINITY(val)) {
        bool pos = copysign(1., val) == 1.;
        data->curcol += pos ? 4 : 5;
        return write_literal(write, pos ? ".inf": "-.inf");
    }
    if (!(converted = TO_UNICODE(obj)))
        return FALSE;
    if (!(encoded = PyUnicode_AsUTF8String(converted)))
        goto exit;
    success = write_object(write, encoded);
    data->curcol += PyBytes_Size(encoded);
  exit:
    Py_XDECREF(encoded);
    Py_DECREF(converted);
    return success;
}

static bool
dump_unicode(PyObject *obj, PyObject *write, dumpdata *data)
{
    bool success = FALSE;
    Py_ssize_t len;
    PyObject *encoded;
    len = PyUnicode_GetLength(obj);
    /* Let's see if we need to quote and escape the string. */
    if (needs_quoting(obj, len)) {
        PyObject *buffer = quote_string(obj, len);
        if (!buffer)
            return FALSE;
        encoded = PyUnicode_AsUTF8String(buffer);
        data->curcol += PyUnicode_GetLength(buffer);
        Py_DECREF(buffer);
    } else {
        encoded = PyUnicode_AsUTF8String(obj);
        data->curcol += len;
    }
    success = write_object(write, encoded);
    Py_DECREF(encoded);
    return success;
}

static bool
dump_bytes(PyObject *obj, PyObject *write, dumpdata *data)
{
    bool success = FALSE;
    char *content;
    Py_ssize_t len;
    PyObject *decoded;
    PyBytes_AsStringAndSize(obj, &content, &len);
    /* Since YAML deals with Unicode strings, require UTF-8 here.
       But we use the "replace" error handler to be lenient. */
    if (!(decoded = PyUnicode_DecodeUTF8(content, len, "replace")))
        return FALSE;
    success = dump_unicode(decoded, write, data);
    Py_DECREF(decoded);
    return success;
}

/* Dump sequence in block representation. */
static bool
dump_seq(PyObject *obj, PyObject *write, dumpdata *data, bool map_value)
{
    bool success = FALSE;
    char buffer[255];
    Py_ssize_t len, i;

    /* Convert to "fast sequence" and determine the length. */
    PyObject *seq = PySequence_Fast(obj, "");
    if (!seq)
        return FALSE;
    len = PySequence_Fast_GET_SIZE(seq);

    if (len == 0) {
        /* Empty list is always inline, and written as []. */
        data->curcol += 2;
        Py_DECREF(seq);
        return write_literal(write, "[]");
    }

    if (map_value) {
        /* In map values, we need to start on a new line. */
        if (!write_literal(write, "\n"))
            goto exit;
        data->curcol = 0;
    }

    for (i = 0; i < len; ++i) {
        PyObject *element = PySequence_Fast_GET_ITEM(seq, i);
        /* Create string with leading indentation and the bullet point. */
        if (!map_value && i == 0) {
            /* In the first item, we don't need to add indentation. */
            buffer[0] = '-';
            make_indent(&buffer[1], data->indentwidth - 1);
        } else if (i <= 1) {
            /* In subsequent lines, we add indentation and bullet point. */
            make_indent(buffer, data->curindent + data->indentwidth);
            buffer[data->curindent] = '-';
        }
        /* Write it out. */
        if (!write_literal(write, buffer))
            goto exit;
        data->curcol += strlen(buffer);
        data->curindent += data->indentwidth;
        /* Write the item. */
        if (!dispatch_dump(element, write, data, FALSE, FALSE))
            goto exit;
        data->curindent -= data->indentwidth;
        /* If the item itself didn't start a new line, do it here. */
        if (data->curcol != 0) {
            if (!write_literal(write, "\n"))
                goto exit;
            data->curcol = 0;
        }
    }
    success = TRUE;
  exit:
    Py_DECREF(seq);
    return success;
}

/* Dump sequence in flowing representation. */
static bool
dump_flowseq(PyObject *obj, PyObject *write, dumpdata *data)
{
    char buffer[255] = "";
    bool success = FALSE;
    Py_ssize_t len, i;
    int indent_to;

    /* Convert to "fast sequence" and determine the length. */
    PyObject *seq = PySequence_Fast(obj, "");
    if (!seq)
        return FALSE;
    len = PySequence_Fast_GET_SIZE(seq);

    /* Write the opening bracket. */
    if (!write_literal(write, "["))
        goto exit;
    data->curcol += 1;
    /* This is what we indent to after a linebreak. */
    indent_to = data->curcol;

    for (i = 0; i < len; ++i) {
        PyObject *element = PySequence_Fast_GET_ITEM(seq, i);
        /* Write the item. */
        if (!dispatch_dump(element, write, data, TRUE, FALSE))
            goto exit;
        /* Break line if over max width. */
        if (i < len - 1 && data->curcol > data->maxwidth - 1) {
            if (buffer[0] == '\0') {
                buffer[0] = ',';
                buffer[1] = '\n';
                make_indent(&buffer[2], indent_to);
            }
            if (!write_literal(write, buffer))
                goto exit;
            data->curcol = indent_to;
        } else {
            /* Write only the separator. */
            if (i < len - 1) {
                if (!write_literal(write, ", "))
                    goto exit;
                data->curcol += 2;
            }
        }
    }
    /* Write the closing bracket. */
    if (!write_literal(write, "]"))
        goto exit;
    data->curcol += 1;

    success = TRUE;
  exit:
    Py_DECREF(seq);
    return success;
}

static bool
dump_dict(PyObject *obj, PyObject *write, dumpdata *data, bool map_value)
{
    char buffer[255];
    bool success = FALSE;
    PyObject *items, *item = NULL, *iter = NULL;
    Py_ssize_t i = 0;

    if (PyDict_Size(obj) == 0) {
        /* Empty dict is always inline {}. */
        data->curcol += 2;
        return write_literal(write, "{}");
    }

    /* Get the iterator over items. */
    if (!(items = PyObject_CallMethod(obj, "items", "")))
        return FALSE;
    if (!(iter = PyObject_GetIter(items)))
        goto exit;

    if (map_value) {
        /* In map values, we need to start on a new line. */
        if (!write_literal(write, "\n"))
            goto exit;
        data->curcol = 0;
    }

    while ((item = PyIter_Next(iter))) {
        assert(PyTuple_Check(item));
        assert(PyTuple_GET_SIZE(item) == 2);
        PyObject *key = PyTuple_GET_ITEM(item, 0);
        PyObject *value = PyTuple_GET_ITEM(item, 1);
        bool value_is_list = (Py_TYPE(value) == &PyList_Type);

        /* Create string with leading indentation. */
        if (!map_value && i == 0) {
            /* In the first item, we don't need to add indentation. */
            buffer[0] = '\0';
        } else if (i <= 1) {
            /* In subsequent lines, we add indentation. */
            make_indent(buffer, data->curindent);
        }

        /* Write it out. */
        if (!write_literal(write, buffer))
            goto exit;
        data->curcol += strlen(buffer);

        /* Write the key. */
        if (!dispatch_dump(key, write, data, TRUE, FALSE))
            goto exit;

        /* Write the separator. We try to be smart and not emit a trailing
           space after the colon if we know the value will start on a new line. */
        if ((value_is_list && PyList_GET_SIZE(value) > 0) ||
            (PyObject_IsInstance(value, (PyObject *)&PyDict_Type) && PyDict_Size(value) > 0) ||
            (PyArray_Check(value) && PyArray_NDIM((PyArrayObject *)value) > 1))
        {
            if (!write_literal(write, ":"))
                goto exit;
            data->curcol += 1;
        } else {
            if (!write_literal(write, ": "))
                goto exit;
            data->curcol += 2;
        }

        /* Special case: no additional indent for lists inside dicts. */
        if (!value_is_list)
            data->curindent += data->indentwidth;
        /* Write the value. */
        if (!dispatch_dump(value, write, data, FALSE, TRUE))
            goto exit;
        if (!value_is_list)
            data->curindent -= data->indentwidth;

        /* If the value itself didn't start a new line, do it here. */
        if (data->curcol != 0) {
            if (!write_literal(write, "\n"))
                goto exit;
            data->curcol = 0;
        }

        Py_DECREF(item);
        i += 1;
    }
    item = NULL;
    /* If PyIter_Next returns NULL, an exception may be set on error. */
    if (!PyErr_Occurred())
        success = TRUE;
  exit:
    Py_XDECREF(item);
    Py_XDECREF(iter);
    Py_DECREF(items);
    return success;
}

/* Dump the innermost slice of a ndarray.

   The most efficient way to do this is to create a tuple of primitive types
   and use the normal flowseq code.
*/
static bool
dump_numpy_inner(PyArrayObject *obj, PyObject *write, dumpdata *data, npy_intp len,
                 PyObject *(*converter)(PyObject *))
{
    npy_intp i;
    bool success = FALSE;
    PyObject *result = PyTuple_New(len);
    if (!result)
        return FALSE;
    for (i = 0; i < len; ++i) {
        PyObject *converted;
        PyObject *item = PySequence_GetItem((PyObject *)obj, i);
        if (!item) {
            Py_DECREF(result);
            return FALSE;
        }
        if (!(converted = converter(item))) {
            Py_DECREF(item);
            Py_DECREF(result);
            return FALSE;
        }
        Py_DECREF(item);
        PyTuple_SET_ITEM(result, i, converted);
    }
    success = dump_flowseq(result, write, data);
    Py_DECREF(result);
    return success;
}

/* Dump numpy array directly: we dump every dimension but the last as a
   mapping with indices as keys, and the last dimension as a flow sequence.

   Only numeric types are supported.
*/
static bool
dump_numpy_array(PyArrayObject *obj, PyObject *write, dumpdata *data, bool map_value)
{
    char buffer[255];
    char index[32];
    npy_intp i;
    int ndim = PyArray_NDIM(obj);
    npy_intp len = PyArray_DIM(obj, 0);

    if (ndim == 1) {
        if (PyArray_ISINTEGER(obj)) {
            return dump_numpy_inner(obj, write, data, len, TO_INT);
        } else if (PyArray_ISFLOAT(obj)) {
            return dump_numpy_inner(obj, write, data, len, PyNumber_Float);
        } else {
            PyErr_SetString(PyExc_ValueError, "only integer and float arrays "
                            "can be dumped");
            return FALSE;
        }
    }

    if (map_value) {
        /* In map values, we need to start on a new line. */
        if (!write_literal(write, "\n"))
            return FALSE;
        data->curcol = 0;
    }

    for (i = 0; i < len; ++i) {
        PyObject *slice = PySequence_GetItem((PyObject *)obj, i);
        if (!slice)
            return FALSE;

        /* Create string with leading indentation. */
        if (data->array_handling == ARRAY_AS_SEQ) {
            if (!map_value && i == 0) {
                /* In the first item, we don't need to add indentation. */
                buffer[0] = '-';
                make_indent(&buffer[1], data->indentwidth - 1);
            } else if (i <= 1) {
                /* In subsequent lines, we add indentation and bullet point. */
                make_indent(buffer, data->curindent + data->indentwidth);
                buffer[data->curindent] = '-';
            }
        } else {
            if (!map_value && i == 0) {
                /* In the first item, we don't need to add indentation. */
                buffer[0] = '\0';
            } else if (i <= 1) {
                /* In subsequent lines, we add indentation. */
                make_indent(buffer, data->curindent);
            }
        }

        /* Write it out. */
        if (!write_literal(write, buffer)) {
            Py_DECREF(slice);
            return FALSE;
        }
        data->curcol += strlen(buffer);

        if (data->array_handling == ARRAY_AS_MAP) {
            /* Write the index. */
            if (ndim == 2)
                snprintf(index, 31, "%" NPY_INTP_FMT ": ", i);
            else
                snprintf(index, 31, "%" NPY_INTP_FMT ":", i);
            if (!write_literal(write, index)) {
                Py_DECREF(slice);
                return FALSE;
            }
            data->curcol += strlen(index);
        }

        data->curindent += data->indentwidth;

        /* Write the value. */
        if (!dump_numpy_array((PyArrayObject *)slice, write, data,
                              data->array_handling == ARRAY_AS_MAP)) {
            Py_DECREF(slice);
            return FALSE;
        }
        Py_DECREF(slice);
        data->curindent -= data->indentwidth;

        /* If the value itself didn't start a new line, do it here. */
        if (data->curcol != 0) {
            if (!write_literal(write, "\n"))
                return FALSE;
            data->curcol = 0;
        }
    }

    return TRUE;
}

/* Main dispatcher for dumping. */
static bool
dispatch_dump(PyObject *obj, PyObject *write, dumpdata *data, bool inline_only, bool map_value)
{
    PyTypeObject *type;
    bool success = FALSE;

    /* Switch on the exact type for primitive types. */
    type = Py_TYPE(obj);
    if (obj == Py_None) {
        success = dump_none(write, data);
    } else if (obj == Py_False || obj == Py_True) {
        success = dump_bool(obj, write, data);
    } else if (type == &PyLong_Type) {
        success = dump_int(obj, write, data);
    } else if (type == &PyFloat_Type) {
        success = dump_float(obj, write, data);
    } else if (type == &PyUnicode_Type) {
        success = dump_unicode(obj, write, data);
    } else if (type == &PyBytes_Type) {
        success = dump_bytes(obj, write, data);
    } else if (type == &flowlist_type) {
        success = dump_flowseq(obj, write, data);
    } else if (PyObject_IsInstance(obj, (PyObject *)&PyList_Type)) {
        /* For lists we allow subtypes to catch e.g. readonlylist. */
        if (inline_only)
            goto disallow;
        success = dump_seq(obj, write, data, map_value);
    } else if (PyObject_IsInstance(obj, (PyObject *)&PyTuple_Type)) {
        if (inline_only)
            goto disallow;
        success = dump_seq(obj, write, data, map_value);
    } else if (PyObject_IsInstance(obj, (PyObject *)&PyDict_Type)) {
        /* For dictionaries we allow subtypes to catch OrderedDicts. */
        if (inline_only)
            goto disallow;
        success = dump_dict(obj, write, data, map_value);
    } else if (PyArray_Check(obj)) {
        success = dump_numpy_array((PyArrayObject *)obj, write, data, map_value);
    } else if (data->callback) {
        /* If we have a callback, try to call it. */
        PyObject *dumped = PyObject_CallFunctionObjArgs(data->callback, obj, NULL);
        if (dumped) {
            /* It needs to return a bytestring. */
            if (PyBytes_Check(dumped)) {
                success = write_object(write, dumped);
            } else {
                PyErr_Format(PyExc_ValueError, "callback must return bytes, not %s",
                             Py_TYPE(dumped)->tp_name);
            }
            Py_DECREF(dumped);
        }
    } else {
        PyErr_Format(PyExc_ValueError, "cannot dump type %s", type->tp_name);
    }

    return success;

  disallow:
    PyErr_Format(PyExc_ValueError, "type %s not allowed as mapping key or "
                 "flow sequence element", type->tp_name);
    return FALSE;
}

/******************************************************************************/
/* Dumper object */

typedef struct {
    PyObject_HEAD
    int array_handling;
    int indentwidth;
    int maxwidth;
    PyObject *callback;
} DumperObject;

/* Constructor, handles converting arguments to instance members. */
static int
dumper_init(DumperObject *self, PyObject *args, PyObject *kwds)
{
    int indentwidth = 4;
    int maxwidth = 120;
    int array_handling = ARRAY_AS_SEQ;
    static char *kwlist[] = {"array_handling", "indent", "width", "callback", 0};
    self->callback = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i|iiO:Dumper", kwlist,
                                     &array_handling, &indentwidth, &maxwidth,
                                     &self->callback))
        return -1;
    Py_XINCREF(self->callback);
    if (indentwidth < 2 || indentwidth > 8) {
        PyErr_SetString(PyExc_ValueError, "indent must be between 2 and 8");
        return -1;
    }
    if (maxwidth <= indentwidth) {
        PyErr_SetString(PyExc_ValueError, "width must be >= indent");
        return -1;
    }
    self->indentwidth = indentwidth;
    self->maxwidth = maxwidth;
    self->array_handling = array_handling;
    return 0;
}

/* Destructor, needs to decrement contained references. */
static void
dumper_dealloc(DumperObject *self)
{
    Py_CLEAR(self->callback);
}

static PyObject *
dumper_dump(DumperObject *self, PyObject *args)
{
    PyObject *obj, *stream, *write;
    dumpdata data;
    data.array_handling = self->array_handling;
    data.indentwidth = self->indentwidth;
    data.maxwidth = self->maxwidth;
    data.callback = self->callback;
    data.curindent = 0;
    data.curcol = 0;

    /* Parse arguments. */
    if (!PyArg_ParseTuple(args, "OO:dump", &obj, &stream))
        return NULL;
    if (!(write = PyObject_GetAttrString(stream, "write")))
        return NULL;

    /* Dump the object. */
    if (!dispatch_dump(obj, write, &data, FALSE, FALSE)) {
        Py_DECREF(write);
        return NULL;
    }

    /* Add a newline if necessary. */
    if (data.curcol != 0)
        if (!write_literal(write, "\n")) {
            Py_DECREF(write);
            return NULL;
        }

    /* If all went ok, return None. */
    Py_DECREF(write);
    Py_RETURN_NONE;
}

static PyMethodDef dumper_methods[] = {
    {"dump", (PyCFunction)dumper_dump, METH_VARARGS, NULL},
    {NULL}
};

static PyMemberDef dumper_members[] = {
    {NULL}
};

static PyTypeObject DumperType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "quickyaml.Dumper",                 /* tp_name */
    sizeof(DumperObject),               /* tp_basicsize */
    0,                                  /* tp_itemsize */
    (destructor)dumper_dealloc,         /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_compare */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT
        | Py_TPFLAGS_BASETYPE,          /* tp_flags */
    0,                                  /* tp_doc */
    0,                                  /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    0,                                  /* tp_iter */
    0,                                  /* tp_iternext */
    dumper_methods,                     /* tp_methods */
    dumper_members,                     /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    (initproc)dumper_init,              /* tp_init */
    PyType_GenericAlloc,                /* tp_alloc */
    PyType_GenericNew,                  /* tp_new */
    0                                   /* tp_free */
};

/******************************************************************************/
/* Module definitions */

static PyMethodDef functions[] = {
    {NULL}
};

static bool
init_inner(PyObject *module)
{
    flowlist_type.tp_base = &PyList_Type;
    if (PyType_Ready(&flowlist_type) < 0)
        return FALSE;

    if (PyType_Ready(&DumperType) < 0)
        return FALSE;

    Py_INCREF((PyObject *)&flowlist_type);
    PyModule_AddObject(module, "flowlist", (PyObject *)&flowlist_type);
    Py_INCREF((PyObject *)&DumperType);
    PyModule_AddObject(module, "Dumper", (PyObject *)&DumperType);

    PyModule_AddObject(module, "ARRAY_AS_SEQ", PyLong_FromLong(ARRAY_AS_SEQ));
    PyModule_AddObject(module, "ARRAY_AS_MAP", PyLong_FromLong(ARRAY_AS_MAP));
    return TRUE;
}

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "quickyaml",
    0,                                  /* m_doc */
    -1,                                 /* m_size */
    functions,                          /* m_methods */
    NULL,                               /* m_reload */
    NULL,                               /* m_traverse */
    NULL,                               /* m_clear */
    NULL                                /* m_free */
};

PyMODINIT_FUNC
PyInit_quickyaml(void)
{
    PyObject *module;

    module = PyModule_Create(&moduledef);
    if (module == NULL)
        return NULL;

    if (!init_inner(module))
        return NULL;

    import_array();
    return module;
}
