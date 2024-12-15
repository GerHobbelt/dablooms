#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "dablooms.h"
#include "structmember.h"

int Py_ModuleVersion = 1;

static PyObject *DabloomsError;

typedef struct {
    PyObject_HEAD
    scaling_bloom_t *filter;    /* Type-specific fields go here. */
} Dablooms;

static void Dablooms_dealloc(Dablooms *self)
{
    if(self->filter)
        free_scaling_bloom(self->filter);

    self->ob_base.ob_type->tp_free((PyObject *)self);
}

static PyObject *Dablooms_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    Dablooms *self = (Dablooms *)type->tp_alloc(type, 0);

    if (self == NULL) {
        return NULL;
    }

    self->filter = NULL;
    return (PyObject *) self;
}

static int Dablooms_init(Dablooms *self, PyObject *args, PyObject *kwds)
{
    double error_rate = 0.1;
    const char *filepath = NULL;
    int capacity = 1;
    static char *kwlist[] = {"capacity", "error_rate", "filepath", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "ids", kwlist,
                                     &capacity, &error_rate, &filepath)) {
        return -1;
    }

    if (capacity < 1){
        PyErr_SetString(DabloomsError, "Bloom creation failed: capacity must be greater than zero");
        return -1;
    }
    if (error_rate > 1 || error_rate < 0){
        PyErr_SetString(DabloomsError, "Bloom creation failed: error_rate must be between 0 and 1");
        return -1;
    }
    if(!(filepath && strlen(filepath))){
        PyErr_SetString(DabloomsError, "Bloom creation failed: filepath required");
        return -1;
    }

    self->filter = new_scaling_bloom(capacity, error_rate, filepath);
    return 0;
}


static int contains(Dablooms *self, PyObject *key)
{
    const char *hash;
    Py_ssize_t len;

    if (!PyArg_Parse(key, "s#", &hash, &len)) {
        return -1;
    }
    return scaling_bloom_check(self->filter, hash, len);
}

static PyObject *check(Dablooms *self, PyObject *args, PyObject *kwds)
{
    const char *hash;
    Py_ssize_t len;
    static char *kwlist[] = {"hash", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#", kwlist, &hash, &len)) {
        return NULL;
    }
    return Py_BuildValue("i", scaling_bloom_check(self->filter, hash, len));
}

static PyObject *add(Dablooms *self, PyObject *args, PyObject *kwds)
{
    const char *hash;
    Py_ssize_t len;
    unsigned long long id;
    static char *kwlist[] = {"hash", "id", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#K", kwlist, &hash, &len, &id)) {
        return NULL;
    }

    return Py_BuildValue("i", scaling_bloom_add(self->filter, hash, len, id));
}

static PyObject *delete(Dablooms *self, PyObject *args, PyObject *kwds)
{
    const char *hash;
    Py_ssize_t len;
    unsigned long long id;
    static char *kwlist[] = {"hash", "id", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#K", kwlist, &hash, &len, &id)) {
        return NULL;
    }

    return Py_BuildValue("i", scaling_bloom_remove(self->filter, hash, len, id));
}

static PyObject *flush(Dablooms *self, PyObject *args)
{
    return Py_BuildValue("i", scaling_bloom_flush(self->filter));
}

static PyObject *mem_seqnum(Dablooms *self, PyObject *args)
{
    return Py_BuildValue("K", scaling_bloom_mem_seqnum(self->filter));
}

static PyObject *disk_seqnum(Dablooms *self, PyObject *args)
{
    return Py_BuildValue("K", scaling_bloom_disk_seqnum(self->filter));
}

static PyMemberDef Dablooms_members[] = {
    {NULL}  /* Sentinel */
};

static PyMethodDef Dablooms_methods[] = {
    {"add",         (PyCFunction)add,         METH_VARARGS | METH_KEYWORDS, "Add an element to the bloom filter."},
    {"delete",      (PyCFunction)delete,      METH_VARARGS | METH_KEYWORDS, "Remove an element from the bloom filter."},
    {"check",       (PyCFunction)check,       METH_VARARGS | METH_KEYWORDS, "Check if an element is in the bloom filter."},
    {"flush",       (PyCFunction)flush,       METH_NOARGS, "Flush a bloom filter to file."},
    {"mem_seqnum",  (PyCFunction)mem_seqnum,  METH_NOARGS, "Get the memory-consistent sequence number."},
    {"disk_seqnum", (PyCFunction)disk_seqnum, METH_NOARGS, "Get the disk-consistent sequence number."},
    {NULL},       /* Sentinel */
};

static PySequenceMethods Dablooms_sequence = {
    .sq_contains = (objobjproc) contains,
};

static PyTypeObject DabloomsType = {
    {PyObject_HEAD_INIT(NULL)},
    .tp_name        = "pydablooms.Dablooms",
    .tp_doc         = PyDoc_STR("Dablooms objects"),
    .tp_basicsize   = sizeof(Dablooms),
    .tp_itemsize    = 0,
    .tp_flags       = Py_TPFLAGS_DEFAULT,
    .tp_new         = Dablooms_new,
    .tp_init        = (initproc)   Dablooms_init,
    .tp_dealloc     = (destructor) Dablooms_dealloc,
    .tp_members     = Dablooms_members,
    .tp_methods     = Dablooms_methods,
    .tp_as_sequence = &Dablooms_sequence,
};

static PyObject *load_dabloom(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    Dablooms *self = PyObject_New(Dablooms, &DabloomsType);
    double error_rate = 0.1;
    const char *filepath = NULL;
    int capacity = 1;
    int result = 0;
    static char *kwlist[] = {"capacity", "error_rate", "filepath", NULL};

    if (! PyArg_ParseTupleAndKeywords(args, kwds, "ids", kwlist,
                                      &capacity, &error_rate, &filepath)) {
        return NULL;
    }

    if (capacity < 1){
        PyErr_SetString(DabloomsError, "Bloom creation failed: capacity must be greater than zero");
        result = -1;
    }
    else if (error_rate > 1 || error_rate < 0){
        PyErr_SetString(DabloomsError, "Bloom creation failed: error_rate must be between 0 and 1");
        result = -1;
    }
    else if(!(filepath && strlen(filepath))){
        PyErr_SetString(DabloomsError, "Bloom creation failed: filepath required");
        result = -1;
    }

    if (result < 0) {
        Dablooms_dealloc(self);
        return NULL;
    }
    self->filter = new_scaling_bloom_from_file(capacity, error_rate, filepath);
    return (PyObject *)self;
}

static PyMethodDef pydablooms_methods[] = {
    {"load_dabloom", (PyCFunction)load_dabloom, METH_VARARGS | METH_KEYWORDS, "Load scaling-bloom from file"},
    {NULL}
};

static struct PyModuleDef pydablooms = {
    PyModuleDef_HEAD_INIT,
    "pydablooms",
    NULL, /* module documentation, may be NULL */
    -1,   /* size of per-interpreter state of the module,
             or -1 if the module keeps state in global variables. */
    pydablooms_methods,
};

PyMODINIT_FUNC PyInit_pydablooms(void)
{
    PyObject *m;
    if (PyType_Ready(&DabloomsType) < 0) {
        return NULL;
    }

    m = PyModule_Create(&pydablooms);
    if (m == NULL) {
        return NULL;
    }

    PyModule_AddObject(m, "__version__", PyUnicode_FromString(dablooms_version()));

    Py_INCREF(&DabloomsType);
    PyModule_AddObject(m, "Dablooms", (PyObject *)&DabloomsType);

    DabloomsError = PyErr_NewException("Dablooms.Error", NULL, NULL);
    Py_INCREF(DabloomsError);
    PyModule_AddObject(m, "error", DabloomsError);
    return m;
}
