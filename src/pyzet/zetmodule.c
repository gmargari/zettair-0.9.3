#include <Python.h>
#include <structmember.h>  /* for PyMemberDef */

#include "firstinclude.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "def.h"
#include "fdset.h"
#include "vec.h"
#include "index.h"
#include "_index.h"
#include "iobtree.h"
#include "vocab.h"
#include "mlparse.h"
#include "str.h"
#include "ndocmap.h"

/* 
 *  Utility function forward declarations.
 */
static PyObject * index_result_to_PyObject(struct index_result * result);
static PyObject * index_results_to_PyObject(struct index_result * results,
  unsigned int num_results, unsigned long int total_results);

/* *****************************************************************
 *
 *  P A R S E R   O B J E C T
 *
 * *****************************************************************/

/*
 *  The actual object.
 */
typedef struct {
    PyObject_HEAD
    struct mlparse parser;
    /* next two are read-only once the parser is created */
    unsigned int wordlen;
    unsigned int lookahead;
    /* buffer that parser works off */
    char * buf;
    unsigned int buflen;
    /* entity buffer, of length wordlen, contents length entity_len */
    char * token;
    unsigned int toklen;
    /* flag to signify that current input is all there is.  This can be
       set at any time. */
    int eof;
} zet_MLParserObject;

/*
 *  Method forward declarations.
 */
static void MLParser_dealloc(zet_MLParserObject * self);
static int MLParser_init(zet_MLParserObject * self, PyObject * args,
  PyObject * kwds);

static PyObject * MLParser_add_input(PyObject * self, PyObject * args);
static PyObject * MLParser_parse(PyObject * self, PyObject * args);
static PyObject * MLParser_eof(PyObject * self, PyObject * args);

/*
 *  Methods visible from Python.
 */
static PyMethodDef MLParser_methods[] = {
    {"add_input", MLParser_add_input, METH_VARARGS,
        "Add input to the current input buffer"},
    {"parse", MLParser_parse, METH_VARARGS,
        "Parse another token from the input"},
    {"eof", MLParser_eof, METH_NOARGS, 
        "Notify parser that current input is all there is"},
    {NULL}
};

/*
 *  Type definition.
 */
static PyTypeObject zet_MLParserType = {
    PyObject_HEAD_INIT(NULL)
    .tp_name         = "zet.MLParser",
    .tp_basicsize    = sizeof(zet_MLParserObject),
    .tp_flags        = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc          = "(SG|X|HT)ML parser",
    .tp_methods      = MLParser_methods,
    .tp_init         = (initproc) MLParser_init,
    .tp_dealloc      = (destructor) MLParser_dealloc
};

/*
 *  Method definitions.
 */

static int MLParser_init(zet_MLParserObject * self, PyObject * args,
  PyObject * kwds) {
    /* Python paramlist is:
       wordlen = 32, lookahead = 999 */
    static char * kwlist[] = {"wordlen", "lookahead", NULL };
    self->wordlen = 32;
    self->lookahead = 999;
    self->buflen = 0;
    self->buf = NULL;
    self->eof = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|II", kwlist,
          &self->wordlen, &self->lookahead))
        return -1;
    if (!mlparse_new(&self->parser, self->wordlen, self->lookahead)) {
        PyErr_SetString(PyExc_StandardError,
          "error initialising parser");
        return -1;
    }
    if ( (self->token = malloc(self->wordlen)) == NULL) {
        PyErr_SetString(PyExc_MemoryError, 
          "out of memory allocating word buffer");
        mlparse_delete(&self->parser);
        return -1;
    }
    self->toklen = 0;
    self->parser.next_in = self->buf;
    self->parser.avail_in = 0;
    return 0;
}

static void MLParser_dealloc(zet_MLParserObject * self) {
    if (self->buf != NULL)
        free(self->buf);
    mlparse_delete(&self->parser);
    self->ob_type->tp_free((PyObject *) self);
}

static PyObject * MLParser_add_input(PyObject * self, PyObject * args) {
    char * input;
    int input_len;
    unsigned int new_len;
    char * new_buf;
    zet_MLParserObject * parser = (zet_MLParserObject *) self;

    if (!PyArg_ParseTuple(args, "s#", &input, &input_len))
        return NULL;
    /* FIXME if old buffer is finished, free it */
    new_len = (unsigned) input_len + parser->buflen;
    if ( (new_buf = realloc(parser->buf, new_len)) == NULL) {
        PyErr_SetString(PyExc_MemoryError, 
          "Out of memory extending parser buffer");
    }
    memcpy(new_buf + parser->buflen, input, input_len);
    parser->parser.avail_in += input_len;
    parser->parser.next_in = new_buf 
        + (parser->parser.next_in - parser->buf);
    parser->buflen = new_len;
    parser->buf = new_buf;
    return Py_BuildValue("i", new_len);
}

static PyObject * MLParser_parse(PyObject * self, PyObject * args) {
    int parse_ret;
    zet_MLParserObject * parser = (zet_MLParserObject *) self;
    int strip = 1;  /* FIXME settable */
    int end = 0;
    int cont = 0;

    parse_ret = mlparse_parse(&parser->parser, parser->token,
      &parser->toklen, strip);
    if (parse_ret == MLPARSE_INPUT) {
        if (parser->eof) {
            mlparse_eof(&parser->parser);
            parse_ret = mlparse_parse(&parser->parser, parser->token,
              &parser->toklen, strip);
        } else {
            /* FIXME need special-purpose exception */
            PyErr_SetString(PyExc_IOError, "out of input");
            return NULL;
        }
    }
    if (parse_ret == MLPARSE_EOF) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    if (parse_ret & MLPARSE_END) {
        end = 1;
        parse_ret ^= MLPARSE_END;
    }
    if (parse_ret & MLPARSE_CONT) {
        cont = 1;
        parse_ret ^= MLPARSE_CONT;
    }
    /* returns tuple:
         (type, token, end?, cont?)
       type has the end and cont flags filtered out.
     */
    return Py_BuildValue("(is#ii)", parse_ret, parser->token,
      parser->toklen, end, cont);
}

static PyObject * MLParser_eof(PyObject * self, PyObject * args) {
    ((zet_MLParserObject *) self)->eof = 1;
    Py_INCREF(Py_None);
    return Py_None;
}

/* *****************************************************************
 *
 *  S E A R C H   R E S U L T   O B J E C T
 *
 * *****************************************************************/

/*
 *  The actual object.
 */
typedef struct {
    PyObject_HEAD
    unsigned long int docno;
    double score;
    PyObject * summary;
    PyObject * title;
    PyObject * auxiliary;
} zet_SearchResultObject;

/*
 *  Members as visible from Python.
 */
static PyMemberDef SearchResult_members[] = {
    {"docno", T_ULONG, offsetof(zet_SearchResultObject, docno), 0,
        "docno"},
    {"score", T_DOUBLE, offsetof(zet_SearchResultObject, score), 0,
        "score"},
    {"summary", T_OBJECT_EX, offsetof(zet_SearchResultObject, summary), 0,
        "summary"},
    {"title", T_OBJECT_EX, offsetof(zet_SearchResultObject, title), 0,
        "title"},
    {"auxiliary", T_OBJECT_EX, offsetof(zet_SearchResultObject, auxiliary), 0,
        "auxiliary"},
    {NULL}
};

/*
 *  Method forward declarations.
 */
static void SearchResult_dealloc(zet_SearchResultObject * self);
static PyObject * SearchResult_reduce(PyObject * self,
  PyObject * ignored_args);
static PyObject * SearchResult_str(zet_SearchResultObject * self);

/*
 *  Methods as visible from Python.
 */
static PyMethodDef SearchResult_methods[] = {
    {"__reduce__", SearchResult_reduce, METH_NOARGS,
        "Used for pickling a search result"},
    {NULL}
};

/*
 *  Type definition.
 */
static PyTypeObject zet_SearchResultType = {
    PyObject_HEAD_INIT(NULL)
    .tp_name        = "zet.SearchResult",
    .tp_basicsize   = sizeof(zet_SearchResultObject),
    .tp_flags       = Py_TPFLAGS_DEFAULT,
    .tp_doc         = "Simple wrapper for an individual document "
                      "search result",
    .tp_methods     = SearchResult_methods,
    .tp_members     = SearchResult_members,
    .tp_dealloc     = (destructor) SearchResult_dealloc,
    .tp_str         = (reprfunc) SearchResult_str,
    .tp_repr        = (reprfunc) SearchResult_str,
};

/*
 *  Method definitions.
 */
static void SearchResult_dealloc(zet_SearchResultObject * self) {
    Py_DECREF(self->summary);
    Py_DECREF(self->title);
    Py_DECREF(self->auxiliary);
    self->ob_type->tp_free((PyObject *) self);
}

static PyObject * SearchResult_reduce(PyObject * self,
  PyObject * ignored_args) {
    zet_SearchResultObject * result = (zet_SearchResultObject *) self;
    PyObject * zetModule = PyImport_ImportModule("zet");
    if (zetModule == NULL)
        return NULL;
    PyObject * fn_name = Py_BuildValue("s", "unpickle_search_result");
    if (fn_name == NULL)
        return NULL;
    PyObject * unpickler = PyDict_GetItem(PyModule_GetDict(zetModule),
      fn_name);
    if (unpickler == NULL) {
        Py_DECREF(fn_name);
        PyErr_SetString(PyExc_NameError, 
          "Can't find zet.unpickle_search_result");
    }
    return Py_BuildValue("O(kdOOO)", unpickler, result->docno, result->score,
      result->summary, result->title, result->auxiliary);
}

static PyObject * SearchResult_str(zet_SearchResultObject * self) {
    PyObject * args_tuple = Py_BuildValue("(kdOOO)", self->docno, 
      self->score, self->summary, self->title, self->auxiliary);
    PyObject * format = PyString_InternFromString("<SearchResult:: docno: %u, "
      "score: %f, summary: %s, title: %s, auxiliary: %s>");
    PyObject * str = PyString_Format(format, args_tuple);
    Py_DECREF(format);
    Py_DECREF(args_tuple);
    return str;
}

/* *****************************************************************
 *
 *  S E A R C H   R E S U L T S   O B J E C T
 *
 * *****************************************************************/

/*
 *  The actual object.
 */
typedef struct {
    PyObject_HEAD
    unsigned long int total_results;
    PyObject * results;
} zet_SearchResultsObject;

/*
 *  Members as visible from Python.
 */
static PyMemberDef SearchResults_members[] = {
    {"total_results", T_ULONG, offsetof(zet_SearchResultsObject, 
      total_results), 0, "total results"},
    {"results", T_OBJECT_EX, offsetof(zet_SearchResultsObject, 
      results), 0, "results" },
    {NULL}
};

/*
 *  Method forward declarations.
 */
static void SearchResults_dealloc(zet_SearchResultsObject * self);

/*
 *  Methods as visible from Python.
 */
static PyMethodDef SearchResults_methods[] = {
    {NULL}
};

/**
 *  Type definition.
 */
static PyTypeObject zet_SearchResultsType = {
    PyObject_HEAD_INIT(NULL)
    .tp_name        = "zet.SearchResults",
    .tp_basicsize   = sizeof(zet_SearchResultsObject),
    .tp_flags       = Py_TPFLAGS_DEFAULT,
    .tp_doc         = "Simple wrapper for search results",
    .tp_methods     = SearchResults_methods,
    .tp_members     = SearchResults_members,
    .tp_dealloc     = (destructor) SearchResults_dealloc
};

/*
 *  Method definitions. 
 */
static void SearchResults_dealloc(zet_SearchResultsObject * self) {
    Py_DECREF(self->results);
    self->ob_type->tp_free((PyObject *) self);
}

/* *****************************************************************
 *
 *  P O S T I N G   O B J E C T
 *
 * *****************************************************************/

/*
 *  The actual object.
 */
typedef struct {
    PyObject_HEAD
    unsigned long docno;
    unsigned long f_dt;
    PyObject * offsets;
} zet_PostingObject;

/*
 *  Members as visible from Python.
 */
static PyMemberDef Posting_members[] = {
    {"docno", T_ULONG, offsetof(zet_PostingObject, docno), 0, 
        "document number"},
    {"f_dt", T_ULONG, offsetof(zet_PostingObject, f_dt), 0,
        "frequency of term in document"},
    {"offsets", T_OBJECT_EX, offsetof(zet_PostingObject, offsets), 0,
        "offsets of term in document, as a tuple"},
    {NULL}
};

/*
 *  Method forward declarations.
 */
static void Posting_dealloc(zet_PostingObject * self);

/*
 *  Object methods as visible from Python.
 */
static PyMethodDef Posting_methods[] = {
    {NULL}
};

/*
 *  Type definition.
 */
static PyTypeObject zet_PostingType = {
    PyObject_HEAD_INIT(NULL)
    .tp_name        = "zet.Posting",
    .tp_basicsize   = sizeof(zet_PostingObject),
    .tp_flags       = Py_TPFLAGS_DEFAULT,
    .tp_doc         = "Wrapper for individual term/doc posting",
    .tp_methods     = Posting_methods,
    .tp_members     = Posting_members,
    .tp_dealloc     = (destructor) Posting_dealloc
};

/*
 *  Method definitions.
 */
static void Posting_dealloc(zet_PostingObject * self) {
    Py_DECREF(self->offsets);
    self->ob_type->tp_free((PyObject *) self);
}

/* *****************************************************************
 *
 *  P O S T I N G S   I T E R A T O R   O B J E C T
 *
 * *****************************************************************/

/*
 *  The postings object.
 *
 *  Must be forward-declared here because the iterator needs to
 *  see its internals (and vice versa).
 */
typedef struct {
    PyObject_HEAD
    char * vec;
    unsigned long size;
    unsigned long docs;
    unsigned long last;
} zet_PostingsObject;

/*
 *  The iterator object.
 */
typedef struct {
    PyObject_HEAD
    unsigned long last_docno;
    unsigned long vec_offset;
    zet_PostingsObject * postings;
} zet_PostingsIteratorObject;


/*
 *  Method forward declarations.
 */
static void PostingsIterator_dealloc(zet_PostingsIteratorObject * self);
static PyObject * 
PostingsIterator_iterator(zet_PostingsIteratorObject * self);
static PyObject * PostingsIterator_next(zet_PostingsIteratorObject * self);
static PyObject * PostingsIterator_get_last_docno(PyObject * self, 
  PyObject * args);
static PyObject * PostingsIterator_skip_to(PyObject * self,
  PyObject * args);

/*
 *  Object methods visible from Python.
 */
static PyMethodDef PostingsIterator_methods[] = {
    {"get_last_docno", PostingsIterator_get_last_docno, METH_NOARGS, 
        "Get the last docno returned by iterator, or None if not started"},
    {"skip_to", PostingsIterator_skip_to, METH_VARARGS,
        "Seek forward to the next posting at or after the given docno"},
    {NULL}
};

/*
 *  Type definition.
 */
static PyTypeObject zet_PostingsIteratorType = {
    PyObject_HEAD_INIT(NULL)
    .tp_name         = "zet.PostingsIterator",
    .tp_basicsize    = sizeof(zet_PostingsIteratorObject),
    .tp_flags        = Py_TPFLAGS_DEFAULT,
    .tp_doc          = "Iterator over a postings list",
    .tp_methods      = PostingsIterator_methods,
    .tp_dealloc      = (destructor) PostingsIterator_dealloc,
    .tp_iter         = (getiterfunc) PostingsIterator_iterator,
    .tp_iternext     = (iternextfunc) PostingsIterator_next
};

/*
 *  Method definitions.
 */
static void PostingsIterator_dealloc(zet_PostingsIteratorObject * self) {
    Py_DECREF((PyObject *) self->postings);
    self->ob_type->tp_free((PyObject *) self);
}

static PyObject * 
PostingsIterator_iterator(zet_PostingsIteratorObject * self) {
    Py_INCREF(self);
    return (PyObject *) self;
}

static PyObject * PostingsIterator_next(zet_PostingsIteratorObject * self) {
    zet_PostingObject * posting;
    struct vec vec;
    unsigned long docno_d;
    unsigned long f_dt;
    unsigned long int offset = 0;
    int i;

    if (self->last_docno != (unsigned long) -1 && 
      self->last_docno >= self->postings->last) {
        return NULL;
    }
    if ( (posting = PyObject_New(zet_PostingObject, &zet_PostingType))
          == NULL)
        return NULL;
    if (PyObject_Init((PyObject *) posting, &zet_PostingType) == NULL) {
        PyObject_Del(posting);
        return NULL;
    }
    /* ok, now we have to start reading through the vector */

    vec.pos = self->postings->vec + self->vec_offset;
    vec.end = vec.pos + self->postings->size;
    vec_vbyte_read_unchecked(&vec, &docno_d);

    if (self->last_docno == (unsigned long) -1)
        posting->docno = docno_d;
    else 
        posting->docno = self->last_docno + docno_d + 1;
    vec_vbyte_read_unchecked(&vec, &f_dt);
    posting->f_dt = f_dt;
    posting->offsets = PyTuple_New(f_dt);
    if (posting->offsets == NULL) {
        PyObject_Del(posting);
        return NULL;
    }

    for (i = 0; i < f_dt; i++) {
        unsigned long int offset_d;
        PyObject * pyOffset;
        vec_vbyte_read_unchecked(&vec, &offset_d);
        if (i == 0)
            offset = offset_d;
        else
            offset = offset + offset_d + 1;
        pyOffset = Py_BuildValue("k", offset);
        if (pyOffset == NULL) {
            Py_DECREF(posting->offsets);
            PyObject_Del(posting);
            return NULL;
        }
        PyTuple_SET_ITEM(posting->offsets, i, pyOffset);
    }

    self->last_docno = posting->docno;
    self->vec_offset = vec.pos - self->postings->vec;
    return (PyObject *) posting;
}

static PyObject * 
PostingsIterator_get_last_docno(PyObject * self, PyObject * args) {
    return Py_BuildValue("k", 
      ((zet_PostingsIteratorObject *) self)->last_docno);
}

static PyObject * PostingsIterator_skip_to(PyObject * self,
  PyObject * args) {
    unsigned long to_docno;
    zet_PostingsIteratorObject * iterator 
        = (zet_PostingsIteratorObject *) self;
    struct vec vec;
    unsigned long curr_docno;
    unsigned long prev_docno;

    if (!PyArg_ParseTuple(args, "k", &to_docno))
        return NULL;
    if (iterator->last_docno != (unsigned long) -1 
      && to_docno <= iterator->last_docno) {
        PyErr_SetString(PyExc_IndexError, "Already past specified docno");
        return NULL;
    }
    vec.pos = iterator->postings->vec + iterator->vec_offset;
    vec.end = vec.pos + iterator->postings->size;

    prev_docno = curr_docno = iterator->last_docno;
    while (curr_docno == (unsigned long) -1 
      || (curr_docno < to_docno && curr_docno < iterator->postings->last)) {
        unsigned long docno_d;
        unsigned long vec_save_pos;

        vec_save_pos = vec.pos - iterator->postings->vec;
        vec_vbyte_read_unchecked(&vec, &docno_d);
        prev_docno = curr_docno;
        if (curr_docno == (unsigned long) -1)
            curr_docno = docno_d;
        else
            curr_docno += (docno_d + 1);
        if (curr_docno < to_docno) {
            /* skip the offsets */
            unsigned long f_dt;
            unsigned int scanned;
            vec_vbyte_read_unchecked(&vec, &f_dt);
            vec_vbyte_scan_unchecked(&vec, f_dt, &scanned);
        } else {
            vec.pos = iterator->postings->vec + vec_save_pos;
        }
    }
    iterator->vec_offset = vec.pos - iterator->postings->vec;
    if (curr_docno >= to_docno)
        iterator->last_docno = prev_docno;
    else
        iterator->last_docno = curr_docno;
    Py_INCREF(Py_None);
    return Py_None;
}

/* *****************************************************************
 * 
 *  P O S T I N G S   O B J E C T
 *
 * *****************************************************************/

/* See above under PostingsIterator for the object definition */

/*
 *  Members as visible from Python.
 */
/*static PyMemberDef Postings_members[] = {
    {NULL}
}; */

/*
 *  Method forward declarations.
 */
static void Postings_dealloc(zet_PostingsObject * self);
static PyObject * Postings_iterator(zet_PostingsObject * self);

/*
 *  Methods as visible from Python.
 */
static PyMethodDef Postings_methods[] = {
    {NULL}
};

/*
 *  Type definition.
 */
static PyTypeObject zet_PostingsType = {
    PyObject_HEAD_INIT(NULL)
    .tp_name        = "zet.Postings",
    .tp_basicsize   = sizeof(zet_PostingsObject),
    .tp_flags       = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc         = "Wrapper for postings list",
    .tp_methods     = Postings_methods,
    .tp_dealloc     = (destructor) Postings_dealloc,
    .tp_iter        = (getiterfunc) Postings_iterator
};

/*
 *  Method definitions.
 */
static void Postings_dealloc(zet_PostingsObject * self) {
    if (self->vec != NULL)
        free(self->vec);
    self->ob_type->tp_free((PyObject *) self);
}

static PyObject * Postings_iterator(zet_PostingsObject * self) {
    zet_PostingsIteratorObject * iterator = PyObject_New
        (zet_PostingsIteratorObject, &zet_PostingsIteratorType);
    if (iterator == NULL)
        return NULL;
    if (PyObject_Init((PyObject *) iterator, 
          &zet_PostingsIteratorType) == NULL) {
        PyObject_Del(iterator);
        return NULL;
    }
    iterator->last_docno = (unsigned long) -1;
    iterator->vec_offset = 0;
    iterator->postings = self;
    Py_INCREF(self);
    return (PyObject *) iterator;
}

/* *****************************************************************
 *
 *  V O C A B   E N T R Y
 *
 *  Returned by the VocabIterator (see next).
 *
 * *****************************************************************/

typedef struct {
    PyObject_HEAD
    PyObject * term;  /* a python string */
    unsigned long int size;
    unsigned long int docs;
    unsigned long int occurs;
    unsigned long int last;
} zet_VocabEntryObject;

static PyMemberDef VocabEntry_members[] = {
    { "term", T_OBJECT_EX, offsetof(zet_VocabEntryObject, term), 0, "term" },
    { "size", T_ULONG, offsetof(zet_VocabEntryObject, size), 0, "size" },
    { "docs", T_ULONG, offsetof(zet_VocabEntryObject, docs), 0, "docs" },
    { "occurs", T_ULONG, offsetof(zet_VocabEntryObject, occurs), 0, "occurs" },
    { "last", T_ULONG, offsetof(zet_VocabEntryObject, last), 0, "last" },
    { NULL }
};

/*
 *  Method forward declarations.
 */
static void VocabEntry_dealloc(zet_VocabEntryObject * self);
static PyObject * VocabEntry_get_term(PyObject * self, PyObject * args);
static PyObject * VocabEntry_get_size(PyObject * self, PyObject * args);
static PyObject * VocabEntry_get_occurs(PyObject * self, PyObject * args);

/*
 *  Object methods as visible from Python.
 */
static PyMethodDef VocabEntry_methods[] = {
    {"get_term", VocabEntry_get_term, METH_NOARGS, "Get the term"},
    {"get_size", VocabEntry_get_size, METH_NOARGS, "Get the size"},
    {"get_occurs", VocabEntry_get_occurs, METH_NOARGS, "Get the occurs"},
    {NULL}
};

/*
 *  Type definition.
 */
static PyTypeObject zet_VocabEntryType = {
    PyObject_HEAD_INIT(NULL)
    .tp_name        = "zet.VocabEntry",
    .tp_basicsize   = sizeof(zet_VocabEntryObject),
    .tp_flags       = Py_TPFLAGS_DEFAULT,
    .tp_doc         = "Wrapper for vocab entry",
    .tp_methods     = VocabEntry_methods,
    .tp_members     = VocabEntry_members,
    .tp_dealloc     = (destructor) VocabEntry_dealloc
};

/*
 *  Method definitions.
 */
static void VocabEntry_dealloc(zet_VocabEntryObject * self) {
    Py_DECREF(self->term);
    self->ob_type->tp_free((PyObject *) self);
}

static PyObject * VocabEntry_get_term(PyObject * self,
  PyObject * args) {
    zet_VocabEntryObject * ref = (zet_VocabEntryObject *) self;
    Py_INCREF(ref->term);
    return ref->term;
}

static PyObject * VocabEntry_get_size(PyObject * self, PyObject * args) {
    zet_VocabEntryObject * ref = (zet_VocabEntryObject *) self;
    return Py_BuildValue("k", ref->size);
}

static PyObject * VocabEntry_get_occurs(PyObject * self, PyObject * args) {
    zet_VocabEntryObject * ref = (zet_VocabEntryObject *) self;
    return Py_BuildValue("k", ref->occurs);
}

/* *****************************************************************
 *
 *  V O C A B   I T E R A T O R
 *
 *  NOTE: currently, this only returns the first vocab entry for
 *  each term, and it assumes that vocab entry is for a doc-ordered
 *  list with word positions.
 *
 * *****************************************************************/

typedef struct zet_IndexObject {
    PyObject_HEAD
    struct index * idx;
} zet_IndexObject;

typedef struct {
    PyObject_HEAD
    unsigned int state[3];
    struct zet_IndexObject * idx;
} zet_VocabIteratorObject;

/*
 *  Method forward declarations.
 *  
 *  NOTE: no allocator, because these can only be made by Index
 *  objects.
 */
static void VocabIterator_dealloc(zet_VocabIteratorObject * self);
static PyObject * VocabIterator_iterator(zet_VocabIteratorObject * self);
static PyObject * VocabIterator_next(zet_VocabIteratorObject * self);

/*
 *  Object methods visible from Python.
 */
static PyMethodDef VocabIterator_methods[] = {
    {NULL}
};

/*
 *  Type definition.
 */
static PyTypeObject zet_VocabIteratorType = {
    PyObject_HEAD_INIT(NULL)
    .tp_name           = "zet.VocabIterator",
    .tp_basicsize      = sizeof(zet_VocabIteratorObject),
    .tp_flags          = Py_TPFLAGS_DEFAULT,
    .tp_doc            = "Iterator over an index's vocab",
    .tp_methods        = VocabIterator_methods,
    .tp_dealloc        = (destructor) VocabIterator_dealloc,
    .tp_iter           = (getiterfunc) VocabIterator_iterator,
    .tp_iternext       = (iternextfunc) VocabIterator_next
};

/*
 *  Method definitions.
 */
static void VocabIterator_dealloc(zet_VocabIteratorObject * self) {
    Py_DECREF((PyObject *) self->idx);
    self->ob_type->tp_free((PyObject *) self);
}

static PyObject * VocabIterator_iterator(zet_VocabIteratorObject * self) {
    /* GOTCHA you must return a _new_ reference to self in this method. */
    Py_INCREF(self);
    return (PyObject *) self;
}

static PyObject * VocabIterator_next(zet_VocabIteratorObject * self) {
    zet_VocabEntryObject * py_vocab_entry;
    const char * term;
    unsigned int termlen;
    void * data;
    unsigned int datalen;
    struct vocab_vector vocab_entry;
    struct vec vec;

    term = iobtree_next_term(self->idx->idx->vocab, self->state, &termlen,
      &data, &datalen);
    if (term == NULL)
        return NULL;

    vec.pos = data;
    vec.end = vec.pos + datalen;
    if ( (vocab_decode(&vocab_entry, &vec)) != VOCAB_OK) {
        PyErr_SetString(PyExc_StandardError, "Unable to decode vocab entry");
        return NULL;
    }

    if ((py_vocab_entry = PyObject_New(zet_VocabEntryObject, 
              &zet_VocabEntryType)) == NULL)
        return NULL;
    if (PyObject_Init((PyObject *) py_vocab_entry, 
          &zet_VocabEntryType) == NULL) {
        PyObject_Del(py_vocab_entry);
        return NULL;
    }
    py_vocab_entry->term = Py_BuildValue("s#", term, (int) termlen);
    if (py_vocab_entry->term == NULL) {
        PyObject_Del(py_vocab_entry);
        return NULL;
    }
    py_vocab_entry->size = vocab_entry.size;
    py_vocab_entry->docs = vocab_docs(&vocab_entry);
    py_vocab_entry->occurs = vocab_occurs(&vocab_entry);
    py_vocab_entry->last = vocab_last(&vocab_entry);
    return (PyObject *) py_vocab_entry;
}

/* *****************************************************************
 *
 *  I N D E X   O B J E C T
 *
 * *****************************************************************/

/*
 *  The actual object.
 *
 *  Forward-declared before the VocabIterator.
 */

/*
 *  Members as visible from Python.
 */
static PyMemberDef Index_members[] = {
    {NULL}
};

/* 
 *  Method forward declarations.
 */
static int Index_init(zet_IndexObject * self, PyObject * args, PyObject * kwds);
static void Index_dealloc(zet_IndexObject * self);

static PyObject * Index_search(PyObject * self, PyObject * args,
  PyObject * kwds); 
static PyObject * Index_retrieve(PyObject * self, PyObject * args, 
  PyObject * kwds);
static PyObject * Index_term_info(PyObject * self, PyObject * args);
static PyObject * Index_term_postings(PyObject * self, PyObject * args);
static PyObject * Index_vocab_iterator(PyObject * self, PyObject * args);
static PyObject * Index_num_docs(PyObject * self, PyObject * args);
static PyObject * Index_vocab_size(PyObject * self, PyObject * args);
static PyObject * Index_doc_aux(PyObject * self, PyObject * args);

/*
 *  Object methods as visible from Python.
 */
static PyMethodDef Index_methods[] = {
    {"search", (PyCFunction) Index_search, METH_VARARGS | METH_KEYWORDS, 
        "Execute search upon an Index object"},
    {"retrieve", (PyCFunction) Index_retrieve, METH_VARARGS | METH_KEYWORDS,
        "Retrieve a document, or portion thereof, from the cache"},
    {"term_info", Index_term_info, METH_VARARGS,
        "Retrieve stats on a term within the index"},
    {"term_postings", Index_term_postings, METH_VARARGS,
        "Retrieve postings for a term"},
    {"vocab_iterator", Index_vocab_iterator, METH_VARARGS,
        "Get an iterator over the vocab of the index"},
    {"num_docs", Index_num_docs, METH_NOARGS, 
        "Get the total number of documents in the indexed collection"},
    {"vocab_size", Index_vocab_size, METH_NOARGS,
        "Get the number of terms in the index vocab" },
    {"doc_aux", Index_doc_aux, METH_VARARGS,
        "Get the auxiliary information for a document."},
    {NULL}
};

/*
 *  Type definition.
 */
static PyTypeObject zet_IndexType = {
    /* NB: C99-style initialisation */
    PyObject_HEAD_INIT(NULL)
    .tp_name        = "zet.Index",
    .tp_basicsize   = sizeof(zet_IndexObject),
    .tp_flags       = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc         = "Wrapper for Zettair index object",
    .tp_methods     = Index_methods,
    .tp_members     = Index_members,
    .tp_init        = (initproc) Index_init,
    .tp_dealloc     = (destructor) Index_dealloc
};

/*
 *  Method definitions.
 */
static void Index_dealloc(zet_IndexObject * self) {
    if (self->idx != NULL)
        index_delete(self->idx);
    self->ob_type->tp_free((PyObject *) self);
}

static int Index_init(zet_IndexObject * self, PyObject * args, 
  PyObject * kwds) {
    char * prefix = "index";
    struct index * idx = NULL;
    int lopts = INDEX_LOAD_NOOPT;
    struct index_load_opt lopt;
    static char * kwlist[] = { "prefix", NULL };

    lopts |= INDEX_LOAD_IGNORE_VERSION;  /* quick hack */
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s", kwlist,
          &prefix))
        return -1;
    if ( (idx = index_load(prefix, MEMORY_DEFAULT, lopts, &lopt)) == NULL) {
        PyErr_SetString(PyExc_StandardError, "Unable to load index");
        return -1;
    }
    self->idx = idx;
    return 0;
}

static PyObject * Index_search(PyObject * self, PyObject * args, 
  PyObject * kwds) {
    char * query;
    char * optType = NULL;
    PyObject * optArgsTuple = NULL;
    unsigned long startdoc;
    unsigned long len;
    zet_IndexObject * Index = (zet_IndexObject *) self;
    struct index_result * result;
    unsigned int results;
    unsigned long int total_results;
    unsigned int accumulator_limit = 0;
    int opts = INDEX_SEARCH_NOOPT;
    struct index_search_opt opt;
    static char * kwlist[] = {"query", "start_doc", "len", "opt_type",
        "opt_args", "accumulator_limit", NULL};
    PyObject * pyResults;

    opt.u.okapi_k3.k1 = 1.2;
    opt.u.okapi_k3.k3 = 1e10;
    opt.u.okapi_k3.b = 0.75;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "skk|sOk", kwlist, &query,
          &startdoc, &len, &optType, &optArgsTuple, &accumulator_limit))
        return NULL;
    if ( (result = malloc(sizeof(*result) * len)) == NULL) {
        PyErr_SetString(PyExc_MemoryError, "Unable to allocate results");
        return NULL;
    }
    if (optType != NULL) {
        if (strcmp(optType, "COSINE") == 0) {
            opts = INDEX_SEARCH_COSINE_RANK;
        } else if (strcmp(optType, "OKAPI") == 0) {
            opts = INDEX_SEARCH_OKAPI_RANK;
        } else if (strcmp(optType, "OKAPI_K3") == 0) {
            if (optArgsTuple == NULL) {
                PyErr_SetString(PyExc_StandardError, "Must supply args to "
                  "search type");
                return NULL;
            }
            opts = INDEX_SEARCH_OKAPI_RANK;
            if (!PyArg_ParseTuple(optArgsTuple, "ddd",
                  &opt.u.okapi_k3.k1, &opt.u.okapi_k3.k3, &opt.u.okapi_k3.b)) {
                return NULL;
            }
        } else if (strcmp(optType, "HAWKAPI") == 0) {
            if (optArgsTuple == NULL) {
                PyErr_SetString(PyExc_StandardError, "Must supply args to "
                  "search type");
                return NULL;
            }
            opts = INDEX_SEARCH_HAWKAPI_RANK;
            if (!PyArg_ParseTuple(optArgsTuple, "dd", &opt.u.hawkapi.alpha,
                  &opt.u.hawkapi.k3)) {
                return NULL;
            }
        } else if (strcmp(optType, "DIRICHLET") == 0) {
            opts = INDEX_SEARCH_DIRICHLET_RANK;
            if (optArgsTuple == NULL || PyTuple_Size(optArgsTuple) == 0) {
                opt.u.dirichlet.mu = 2500.0;
            } else if (!PyArg_ParseTuple(optArgsTuple, "f",
                  &opt.u.dirichlet.mu)) {
                return NULL;
            }
        } else {
            PyErr_SetString(PyExc_StandardError, "Unknown search type");
            return NULL;
        }
    }
    if (accumulator_limit != 0) {
        opts |= INDEX_SEARCH_ACCUMULATOR_LIMIT;
        opt.accumulator_limit = accumulator_limit;
    }
    if (!index_search(Index->idx, query, startdoc, len,
          result, &results, &total_results, opts, &opt)) {
        char err_buf[1024];
        snprintf(err_buf, 1024, "Unable to perform search for query '%s'; "
           "system error is '%s'\n", query, strerror(errno));
        PyErr_SetString(PyExc_StandardError, err_buf);
        free(result);
        return NULL;
    }
    pyResults = index_results_to_PyObject(result, results, total_results);
    free(result);
    return pyResults;
}

static PyObject * Index_retrieve(PyObject * self, PyObject * args, 
  PyObject * kwds) {
    unsigned long int docno;
    unsigned long int offset = 0;
    char * dst = NULL;
    unsigned int len = 0;
    unsigned int retrieved_len;
    PyObject * doc = NULL;
    static char * kwlist[] = { "docno", "offset", "len", NULL };
    zet_IndexObject * Index = (zet_IndexObject *) self;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "k|kI", kwlist, &docno,
          &offset, &len)) 
        return NULL;
    if (len == 0) {
        unsigned int bytes;

        bytes = index_retrieve_doc_bytes(Index->idx, docno);
        if (bytes == UINT_MAX) {
            PyErr_SetString(PyExc_StandardError, 
              "Unable to retrieve doc stats");
            return NULL;
        }
        len = bytes - offset;
    }
    if ( (dst = malloc(len)) == NULL) {
        PyErr_SetString(PyExc_MemoryError, "Out of memory");
        return NULL;
    }
    retrieved_len = index_retrieve(Index->idx, docno, offset,
       dst, len);
    if (retrieved_len == (unsigned int) -1) {
        PyErr_SetString(PyExc_StandardError, "Error retrieving document");
    } else {
        doc = Py_BuildValue("s#", dst, (int) retrieved_len);
    }

    free(dst);
    return doc;
}

/* XXX should return the VocabEntry object defined above for the
 * vocab iterator */
static PyObject * Index_term_info(PyObject * self, PyObject * args) {
    zet_IndexObject * Index = (zet_IndexObject *) self;
    struct iobtree * vocab = Index->idx->vocab;
    char * term;
    unsigned int termlen;
    void * term_data;
    unsigned int veclen;
    struct vocab_vector ve;
    struct vec vec;

    if (!PyArg_ParseTuple(args, "s", &term))
        return NULL;
    termlen = strlen(term);
    term_data = iobtree_find(vocab, term, termlen, 0, &veclen);
    if (term_data == NULL) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    vec.pos = term_data;
    vec.end = term_data + veclen;
    if (vocab_decode(&ve, &vec) != VOCAB_OK) {
        PyErr_SetString(PyExc_StandardError, "Error decoding vocab vector");
        return NULL;
    }
    /* XXX handle other types of vocab vector */
    if (ve.type != VOCAB_VTYPE_DOCWP) {
        PyErr_SetString(PyExc_StandardError, "Expected first vocab vector "
          "entry to be doc-ordered with word positions, but this was not "
          "the case");
        return NULL;
    }
    return Py_BuildValue("(kkkk)", ve.header.docwp.docs, 
      ve.header.docwp.occurs, ve.header.docwp.last, ve.size);
}

static PyObject * Index_term_postings(PyObject * self, PyObject * args) {
    zet_IndexObject * Index = (zet_IndexObject *) self;
    struct index * idx = Index->idx;
    struct iobtree * vocab = idx->vocab;
    char * term;
    unsigned int termlen;
    struct vocab_vector ve;
    struct vec vec;
    void * term_data;
    unsigned int veclen;
    int fd;
    zet_PostingsObject * postings;

    /* FIXME copy of above */
    if (!PyArg_ParseTuple(args, "s", &term))
        return NULL;
    termlen = strlen(term);
    term_data = iobtree_find(vocab, term, termlen, 0, &veclen);
    if (term_data == NULL) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    vec.pos = term_data;
    vec.end = term_data + veclen;
    if (vocab_decode(&ve, &vec) != VOCAB_OK) {
        PyErr_SetString(PyExc_StandardError, "Error decoding vocab entry");
        return NULL;
    }
    /* XXX handle other types of vocab vector */
    if (ve.type != VOCAB_VTYPE_DOCWP) {
        PyErr_SetString(PyExc_StandardError, "Expected first vocab vector "
          "entry to be doc-ordered with word positions, but this was not "
          "the case");
        return NULL;
    }
    if (ve.location != VOCAB_LOCATION_FILE) {
        PyErr_SetString(PyExc_StandardError, 
          "I only handle on-file vectors");
        return NULL;
    }
    if ( (postings = PyObject_New(zet_PostingsObject, 
              &zet_PostingsType)) == NULL) {
        return NULL;
    }
    if (PyObject_Init((PyObject *) postings, &zet_PostingsType) == NULL) {
        PyObject_Del(postings);
        return NULL;
    }
    postings->size = ve.size;
    postings->docs = ve.header.docwp.docs;
    postings->last = ve.header.docwp.last;
    if ( (postings->vec = malloc(ve.size)) == NULL) {
        PyErr_SetString(PyExc_MemoryError, 
          "Out of memory allocating vector buffer");
        PyObject_Del(postings);
        return NULL;
    }
    fd = fdset_pin(idx->fd, idx->index_type, ve.loc.file.fileno, 
      ve.loc.file.offset, SEEK_SET);
    if (fd == -1 || read(fd, postings->vec, ve.size) < ve.size) {
        PyErr_SetString(PyExc_IOError, "Unable to read from vector file");
        free(postings->vec);
        PyObject_Del(postings);
        return NULL;
    }
    fdset_unpin(idx->fd, idx->index_type, ve.loc.file.fileno, fd);
    return (PyObject *) postings;
}

static PyObject * Index_vocab_iterator(PyObject * self, PyObject * args) {
    zet_VocabIteratorObject * iterator = PyObject_New
        (zet_VocabIteratorObject, &zet_VocabIteratorType);
    if (iterator == NULL)
        return NULL;
    if (PyObject_Init((PyObject *) iterator, &zet_VocabIteratorType) == NULL) {
        PyObject_Del(iterator);
        return NULL;
    }

    iterator->state[0] = 0;
    iterator->state[1] = 0;
    iterator->state[2] = 0;
    iterator->idx = (zet_IndexObject *) self;
    Py_INCREF(self);
    return (PyObject *) iterator;
}

static PyObject * Index_num_docs(PyObject * self, PyObject * args) {
    zet_IndexObject * Index = (zet_IndexObject *) self;
    struct index * idx = Index->idx;
    unsigned long num_docs;

    num_docs = ndocmap_entries(idx->map);
    return Py_BuildValue("k", num_docs);
}

static PyObject * Index_vocab_size(PyObject * self, PyObject * args) {
    zet_IndexObject * Index = (zet_IndexObject *) self;
    struct index * idx = Index->idx;
    unsigned long vocab_size;

    vocab_size = iobtree_size(idx->vocab);
    return Py_BuildValue("k", vocab_size);
}

#define AUX_BUF_LEN 1024

static PyObject * Index_doc_aux(PyObject * self, PyObject * args) {
    zet_IndexObject * Index = (zet_IndexObject *) self;
    struct index * idx = Index->idx;
    struct ndocmap * docmap = idx->map;
    char aux_buf[AUX_BUF_LEN];
    unsigned aux_len;
    unsigned long int docno;
    enum ndocmap_ret ret;

    if (!PyArg_ParseTuple(args, "k", &docno))
        return NULL;
    ret = ndocmap_get_aux(docmap, docno, aux_buf, AUX_BUF_LEN, &aux_len);
    if (ret != NDOCMAP_OK) {
        /* error might be NDOCMAP_BUFSIZE_ERROR, but life is too short... */
        PyErr_SetString(PyExc_IOError, "Unable to read aux info");
        return NULL;
    }
    return Py_BuildValue("s#", aux_buf, (int) aux_len);
}

/* *****************************************************************
 *
 * U T I L I T Y   F U N C T I O N S
 *
 * *****************************************************************/

static PyObject * index_result_to_PyObject(struct index_result * result) {
    zet_SearchResultObject * pyResult = PyObject_New(zet_SearchResultObject,
      &zet_SearchResultType);
    if (pyResult == NULL)
        return NULL;
    if (PyObject_Init((PyObject *) pyResult, 
          &zet_SearchResultType) == NULL) {
        PyObject_Del(pyResult); /* not DECREF, as not fully initialised */
        return NULL;
    }
    pyResult->docno = result->docno;
    pyResult->score = result->score;
    if ((pyResult->summary = Py_BuildValue("z", result->summary)) == NULL) {
        PyObject_Del(pyResult);
        return NULL;
    }
    if ((pyResult->title = Py_BuildValue("z", result->title)) == NULL) {
        Py_DECREF(pyResult->summary);
        PyObject_Del(pyResult);
        return NULL;
    }
    if ((pyResult->auxiliary = Py_BuildValue("z", 
              result->auxilliary)) == NULL) {
        Py_DECREF(pyResult->summary);
        Py_DECREF(pyResult->auxiliary);
        PyObject_Del(pyResult);
        return NULL;
    }
    return (PyObject *) pyResult;
    /*PyObject * tuple = Py_BuildValue("(kdsss)", result->docno,
      result->score, result->summary, result->title, result->auxilliary);
    return tuple; */
}

static PyObject * index_results_to_PyObject(struct index_result * results,
  unsigned int num_results, unsigned long int total_results) {
    int i;
    zet_SearchResultsObject * pyResult = PyObject_New(zet_SearchResultsObject,
      &zet_SearchResultsType);
    if (pyResult == NULL)
        return NULL;
    if (PyObject_Init((PyObject *) pyResult, 
          &zet_SearchResultsType) == NULL) {
        PyObject_Del(pyResult); /* not DECREF, as not fully initialised */
        return NULL;
    }
    PyObject * results_tuple = PyTuple_New(num_results);
    if (results_tuple == NULL) {
        PyObject_Del(pyResult); /* not DECREF, as not fully initialised */
        return NULL;
    }
    pyResult->results = results_tuple;
    pyResult->total_results = total_results;
    /* from here, pyResult is fully initialised */
    for (i = 0; i < num_results; i++) {
        PyObject * result_tuple = index_result_to_PyObject(&results[i]);
        if (result_tuple == NULL) {
            Py_DECREF(pyResult);
            return NULL;
        }
        PyTuple_SET_ITEM(results_tuple, i, result_tuple);
    }
    return (PyObject *) pyResult;
}

/* *****************************************************************
 *
 * Z E T   M O D U L E
 *
 * *****************************************************************/

/*
 *  Module method forward declarations.
 */
static PyObject * zet_search(PyObject *self, PyObject *args);
static PyObject * zet_extract_words(PyObject *self, PyObject *args);
static PyObject * zet_unpickle_search_result(PyObject * self, PyObject * args);
static PyObject * zet_hash(PyObject * self, PyObject * args);

/*
 *  Methods as visible from Python.
 */
static PyMethodDef ZetMethods[] = {
    {"search", zet_search, METH_VARARGS, "Execute a zettair search."},
    {"extract_words", zet_extract_words, METH_VARARGS, 
        "Extract a list of words from a string, using the Zettair parser"},
    {"unpickle_search_result", zet_unpickle_search_result, METH_VARARGS,
        "Constructor called when unpickling search results"},
    {"hash", zet_hash, METH_VARARGS, 
        "Hash a string according to the zettair's hash algorithm"},
    { NULL, NULL, 0, NULL }
};

/*
 *  Method definitions.
 */
static PyObject *
zet_search(PyObject *self, PyObject *args) {
    char * prefix;
    char * query;
    unsigned long startdoc;
    unsigned long len;
    struct index * idx;
    struct index_result * result;
    unsigned int results;
    unsigned long int total_results;
    int opts = INDEX_SEARCH_NOOPT;
    struct index_search_opt opt;

    if (!PyArg_ParseTuple(args, "sskk", &prefix, &query, &startdoc,
          &len))
        return NULL;
    if ( (idx = index_load(prefix, MEMORY_DEFAULT,
              INDEX_LOAD_NOOPT, NULL)) == NULL) {
        PyErr_SetString(PyExc_StandardError, "Unable to load index");
        return NULL;
    }
    if ( (result = malloc(sizeof(*result) * len)) == NULL) {
        PyErr_SetString(PyExc_MemoryError, "Unable to allocate results");
        index_delete(idx);
        return NULL;
    }
    if (!index_search(idx, query, startdoc, len, result, 
          &results, &total_results, opts, &opt)) {
        PyErr_SetString(PyExc_StandardError, "Unable to perform search");
        free(result);
        index_delete(idx);
        return NULL;
    }
    PyObject * results_tuple = index_results_to_PyObject(result, results,
      total_results);
    free(result);
    index_delete(idx);
    return results_tuple;
}

static PyObject * zet_extract_words(PyObject *self, PyObject *args) {
    char * buf;
    unsigned int wordlen = TERMLEN_DEFAULT;
    unsigned int lookahead = LOOKAHEAD;
    int limit = -1;
    int word_count = 0;
    struct mlparse parser;
    char * word_buf;
    unsigned int len;
    int buflen;
    int parse_ret;
    PyObject * word_list;

    if (!PyArg_ParseTuple(args, "s#|iII", &buf, &buflen, &limit, 
          &wordlen, &lookahead))
        return NULL;
    if ( (word_list = PyList_New(0)) == NULL)
        return NULL;
    if ( (word_buf = malloc(wordlen + 1)) == NULL) {
        PyErr_SetString(PyExc_MemoryError, "Allocating word buffer");
        Py_DECREF(word_list);
        return NULL;
    }
    if (!mlparse_new(&parser, wordlen, lookahead)) {
        PyErr_SetString(PyExc_StandardError, "Unable to initialise parser");
        free(word_buf);
        Py_DECREF(word_list);
        return NULL;
    }
    parser.next_in = buf;
    parser.avail_in = buflen;
    while ( (parse_ret = mlparse_parse(&parser, word_buf, &len, 1)) 
      != MLPARSE_EOF && (limit < 0 || word_count < limit)) {
        if (parse_ret == MLPARSE_WORD || parse_ret == (MLPARSE_WORD
          | MLPARSE_END)) {
            PyObject * pyWord = Py_BuildValue("s#", word_buf, len);
            if (pyWord == NULL || PyList_Append(word_list, pyWord) < 0) {
                free(word_buf);
                Py_DECREF(word_list);
                if (pyWord != NULL) {
                    Py_DECREF(pyWord);
                }
                return NULL;
            }
            word_count++;
            Py_DECREF(pyWord);
        } else if (parse_ret == MLPARSE_INPUT) {
            mlparse_eof(&parser);
        }
    }
    mlparse_delete(&parser);
    return word_list;
}

/* NOTE: this is a module method, _NOT_ a class method */
static PyObject * zet_unpickle_search_result(PyObject * self, PyObject * args) {
    unsigned long docno;
    double score;
    PyObject * summary;
    PyObject * title;
    PyObject * auxiliary;
    zet_SearchResultObject * pyResult;

    if (!PyArg_ParseTuple(args, "kdSSS", &docno, &score, &summary,
          &title, &auxiliary))
        return NULL;
    if ( (pyResult = PyObject_New(zet_SearchResultObject, 
              &zet_SearchResultType)) == NULL)
        return NULL;
    pyResult->docno = docno;
    pyResult->score = score;
    pyResult->summary = summary;
    pyResult->title = title;
    pyResult->auxiliary = auxiliary;
    Py_INCREF(summary);
    Py_INCREF(title);
    Py_INCREF(auxiliary);
    return (PyObject *) pyResult;
}

static PyObject *
zet_hash(PyObject *self, PyObject *args) {
    char * string;
    unsigned hval;
    int modulus = -1;
    if (!PyArg_ParseTuple(args, "s|k", &string, &modulus))
        return NULL;
    hval = str_hash(string);
    if (modulus > 0)
        hval %= modulus;
    return Py_BuildValue("i", hval);
}

/*
 *  Module initialization.
 */
PyMODINIT_FUNC initzet(void) {
    PyObject * m;

    /* Index type initialisation */
    zet_IndexType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&zet_IndexType) < 0)
        return;

    /* SearchResult type initialisation */
    zet_SearchResultType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&zet_SearchResultType) < 0)
        return;

    /* SearchResults type initialisation */
    zet_SearchResultsType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&zet_SearchResultsType) < 0)
        return;

    /* Posting type initialisation */
    zet_PostingType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&zet_PostingType) < 0)
        return;

    /* Postings type initialisation */
    zet_PostingsType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&zet_PostingsType) < 0)
        return;

    /* PostingsIterator type initialisation */
    zet_PostingsIteratorType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&zet_PostingsIteratorType) < 0)
        return;

    /* VocabEntry type initialisation */
    zet_VocabEntryType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&zet_VocabEntryType) < 0)
        return;

    /* VocabIterator type initialisation */
    zet_VocabIteratorType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&zet_VocabIteratorType) < 0)
        return;

    /* MLParser type initialisation */
    zet_MLParserType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&zet_MLParserType) < 0)
        return;

    /* Module initialisation */
    m = Py_InitModule3("zet", ZetMethods, "Module that wraps Zettair");
    if (m == NULL)
        return;

    /* Adding Index type to module */
    Py_INCREF(&zet_IndexType);
    PyModule_AddObject(m, "Index", (PyObject *) &zet_IndexType);

    /* Adding SearchResults type to module */
    Py_INCREF(&zet_SearchResultType);
    PyModule_AddObject(m, "SearchResult", (PyObject *) &zet_SearchResultType);

    /* Adding SearchResults type to module */
    Py_INCREF(&zet_SearchResultsType);
    PyModule_AddObject(m, "SearchResults", (PyObject *) &zet_SearchResultsType);

    /* Adding Posting type to module */
    Py_INCREF(&zet_PostingType);
    PyModule_AddObject(m, "Posting", (PyObject *) &zet_PostingType);

    /* Adding Postings type to module */
    Py_INCREF(&zet_PostingsType);
    PyModule_AddObject(m, "Postings", (PyObject *) &zet_PostingsType);

    /* Adding PostingsIterator type to module */
    Py_INCREF(&zet_PostingsIteratorType);
    PyModule_AddObject(m, "PostingsIterator", 
      (PyObject *) &zet_PostingsIteratorType);

    /* Adding VocabEntry type to module */
    Py_INCREF(&zet_VocabEntryType);
    PyModule_AddObject(m, "VocabEntry", (PyObject *) &zet_VocabEntryType);

    /* Adding VocabIterator type to module */
    Py_INCREF(&zet_VocabIteratorType);
    PyModule_AddObject(m, "VocabIterator", (PyObject *) &zet_VocabIteratorType);

    /* Adding MLParser type to module */
    Py_INCREF(&zet_MLParserType);
    PyModule_AddObject(m, "MLParser", (PyObject *) &zet_MLParserType);
}
