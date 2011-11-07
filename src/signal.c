
/*
 * This is not a true signal handler but it does the job. It uses a Prepare handle, which
 * gets executed one per loop iteration, before blocking for I/O, to call PyErr_CheckSignals.
 * That will cause standard registered signal handlers to be called by Python.
 */

static PyObject* PyExc_SignalError;


static void
on_signal_close(uv_handle_t *handle)
{
    PyGILState_STATE gstate = PyGILState_Ensure();
    ASSERT(handle);
    handle->data = NULL;
    PyMem_Free(handle);
    PyGILState_Release(gstate);
}


static void
on_signal_callback(uv_prepare_t *handle, int status)
{
    PyGILState_STATE gstate = PyGILState_Ensure();
    ASSERT(handle);
    ASSERT(status == 0);

    Signal *self = (Signal *)handle->data;
    ASSERT(self);
    /* Object could go out of scope in the callback, increase refcount to avoid it */
    Py_INCREF(self);

    if (PyErr_CheckSignals() < 0) {
        PyErr_Print();
    }

    Py_DECREF(self);
    PyGILState_Release(gstate);
}


static PyObject *
Signal_func_start(Signal *self)
{
    if (self->closed) {
        PyErr_SetString(PyExc_SignalError, "Signal is closed");
        return NULL;
    }

    int r = uv_prepare_start(self->uv_prepare, on_signal_callback);
    if (r != 0) {
        raise_uv_exception(self->loop, PyExc_SignalError);
        return NULL;
    }

    Py_RETURN_NONE;
}


static PyObject *
Signal_func_stop(Signal *self)
{
    if (self->closed) {
        PyErr_SetString(PyExc_SignalError, "Signal is already closed");
        return NULL;
    }

    int r = uv_prepare_stop(self->uv_prepare);
    if (r != 0) {
        raise_uv_exception(self->loop, PyExc_SignalError);
        return NULL;
    }

    Py_RETURN_NONE;
}


static PyObject *
Signal_func_close(Signal *self)
{
    if (self->closed) {
        PyErr_SetString(PyExc_SignalError, "Signal is already closed");
        return NULL;
    }

    self->closed = True;
    uv_close((uv_handle_t *)self->uv_prepare, on_signal_close);

    Py_RETURN_NONE;
}


static int
Signal_tp_init(Signal *self, PyObject *args, PyObject *kwargs)
{
    int r = 0;
    Loop *loop;
    PyObject *tmp = NULL;
    uv_prepare_t *uv_prepare = NULL;

    if (self->initialized) {
        PyErr_SetString(PyExc_SignalError, "Object already initialized");
        return -1;
    }

    if (!PyArg_ParseTuple(args, "O!:__init__", &LoopType, &loop)) {
        return -1;
    }

    tmp = (PyObject *)self->loop;
    Py_INCREF(loop);
    self->loop = loop;
    Py_XDECREF(tmp);

    uv_prepare = PyMem_Malloc(sizeof(uv_prepare_t));
    if (!uv_prepare) {
        PyErr_NoMemory();
        Py_DECREF(loop);
        return -1;
    }

    r = uv_prepare_init(SELF_LOOP, uv_prepare);
    if (r != 0) {
        raise_uv_exception(self->loop, PyExc_SignalError);
        Py_DECREF(loop);
        return -1;
    }
    uv_prepare->data = (void *)self;
    self->uv_prepare = uv_prepare;

    self->initialized = True;
    self->closed = False;

    return 0;
}


static PyObject *
Signal_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    Signal *self = (Signal *)PyType_GenericNew(type, args, kwargs);
    if (!self) {
        return NULL;
    }
    self->initialized = False;
    return (PyObject *)self;
}


static int
Signal_tp_traverse(Signal *self, visitproc visit, void *arg)
{
    Py_VISIT(self->loop);
    return 0;
}


static int
Signal_tp_clear(Signal *self)
{
    Py_CLEAR(self->loop);
    return 0;
}


static void
Signal_tp_dealloc(Signal *self)
{
    if (!self->closed) {
        uv_close((uv_handle_t *)self->uv_prepare, on_signal_close);
    }
    Signal_tp_clear(self);
    Py_TYPE(self)->tp_free((PyObject *)self);
}


static PyMethodDef
Signal_tp_methods[] = {
    { "start", (PyCFunction)Signal_func_start, METH_VARARGS|METH_KEYWORDS, "Start the Signal." },
    { "stop", (PyCFunction)Signal_func_stop, METH_NOARGS, "Stop the Signal." },
    { "close", (PyCFunction)Signal_func_close, METH_NOARGS, "Close the Signal." },
    { NULL }
};


static PyMemberDef Signal_tp_members[] = {
    {"loop", T_OBJECT_EX, offsetof(Signal, loop), READONLY, "Loop where this Signal is running on."},
    {NULL}
};


static PyTypeObject SignalType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "pyuv.Signal",                                                  /*tp_name*/
    sizeof(Signal),                                                 /*tp_basicsize*/
    0,                                                              /*tp_itemsize*/
    (destructor)Signal_tp_dealloc,                                  /*tp_dealloc*/
    0,                                                              /*tp_print*/
    0,                                                              /*tp_getattr*/
    0,                                                              /*tp_setattr*/
    0,                                                              /*tp_compare*/
    0,                                                              /*tp_repr*/
    0,                                                              /*tp_as_number*/
    0,                                                              /*tp_as_sequence*/
    0,                                                              /*tp_as_mapping*/
    0,                                                              /*tp_hash */
    0,                                                              /*tp_call*/
    0,                                                              /*tp_str*/
    0,                                                              /*tp_getattro*/
    0,                                                              /*tp_setattro*/
    0,                                                              /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE,  /*tp_flags*/
    0,                                                              /*tp_doc*/
    (traverseproc)Signal_tp_traverse,                               /*tp_traverse*/
    (inquiry)Signal_tp_clear,                                       /*tp_clear*/
    0,                                                              /*tp_richcompare*/
    0,                                                              /*tp_weaklistoffset*/
    0,                                                              /*tp_iter*/
    0,                                                              /*tp_iternext*/
    Signal_tp_methods,                                              /*tp_methods*/
    Signal_tp_members,                                              /*tp_members*/
    0,                                                              /*tp_getsets*/
    0,                                                              /*tp_base*/
    0,                                                              /*tp_dict*/
    0,                                                              /*tp_descr_get*/
    0,                                                              /*tp_descr_set*/
    0,                                                              /*tp_dictoffset*/
    (initproc)Signal_tp_init,                                       /*tp_init*/
    0,                                                              /*tp_alloc*/
    Signal_tp_new,                                                  /*tp_new*/
};


