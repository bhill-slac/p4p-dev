#ifndef P4P_H
#define P4P_H

#include <sstream>
#include <algorithm>
#include <stdexcept>

#ifdef READONLY
// don't want def from shareLib.h
#  undef READONLY
#endif

#include <Python.h>
#include <structmember.h>

#include <epicsMutex.h>
#include <epicsGuard.h>
#include <pv/pvIntrospect.h>
#include <pv/bitSet.h>
#include <pv/pvData.h>
#include <pv/pvAccess.h>

typedef epicsGuard<epicsMutex> Guard;
typedef epicsGuardRelease<epicsMutex> UnGuard;

struct SB {
    std::ostringstream strm;
    operator std::string() { return strm.str(); }
    template<typename T>
    SB& operator<<(const T& v) {
        strm<<v;
        return *this;
    }
};

struct PyExternalRef;

struct borrow {};
struct allownull {};
struct nextiter {};

struct PyRef {
    PyObject *obj;
    PyRef() :obj(0) {}
    PyRef(const PyRef& o) :obj(o.obj) {
        Py_XINCREF(obj);
    }
    //! Make a new reference using the provided ref
    PyRef(PyObject *o, const borrow&) :obj(o) {
        if(!o)
            throw std::runtime_error("Can't borrow NULL");
        Py_INCREF(obj);
    }
    //! Take over the provided ref as our own.  Accept NULL silently.
    PyRef(PyObject *o, const allownull&) :obj(o) {}
    //! Same as allownull except that NULL is allowed as long as !PyErr_Occurred()
    //! For use with PyIter_Next()
    PyRef(PyObject *o, const nextiter&) :obj(o) {
        if(!o && PyErr_Occurred())
            throw std::runtime_error("XXX"); // exception already set
    }
    //! Take over the provided ref as our own.  Throw if the provided ref. is NULL
    explicit PyRef(PyObject *o) :obj(o) {
        if(!o)
            throw std::runtime_error("Alloc failed");
    }
    explicit PyRef(const PyExternalRef& o);
    ~PyRef() {
        Py_CLEAR(obj);
    }
    PyRef& operator=(const PyRef& rhs) {
        Py_XINCREF(rhs.obj);
        PyObject *temp = obj;
        obj = rhs.obj;
        Py_XDECREF(temp);
        return *this;
    }
    void reset(PyObject *o=0) {
        std::swap(obj, o);
        Py_XDECREF(o);
    }
    void reset(PyObject *o, const borrow&) {
        if(!o)
            throw std::runtime_error("Can't borrow NULL");
        Py_INCREF(o);
        std::swap(obj, o);
        Py_XDECREF(o);
    }
    PyObject *release() {
        PyObject *o=0;
        std::swap(obj, o);
        return o;
    }
    PyObject* get() const { return obj; }

    inline bool valid() const { return !!obj; }

#if __cplusplus>=201103L
    explicit operator bool() const { return valid(); }
#else
private:
    typedef bool (PyRef::*bool_type)() const;
public:
    operator bool_type() const { return !!obj ? &PyRef::valid : 0; }
#endif

    void swap(PyRef& o) {
        std::swap(obj, o.obj);
    }
    struct CollectReturn {
        PyObject *V;
        PyRef& R;
        CollectReturn(PyRef& R) :V(0), R(R) {}
        ~CollectReturn() {
            R.reset(V);
        }
        PyObject **get() { return &V; }
    };
    CollectReturn collect() { return CollectReturn(*this); }
};

struct PyString
{
    PyObject *base;
    PyRef temp;
    explicit PyString(PyObject *b) :base(b) {
        if(PyUnicode_Check(b)) {
            temp.reset(PyUnicode_AsUTF8String(b));
            if(!temp)
                throw std::runtime_error("PyString Unicode Error");
        } else if(!PyBytes_Check(b))
            throw std::runtime_error("Not bytes or unicode");
    }
    std::string str() {
        PyObject *X = temp ? temp.get() : base;
        return std::string(PyBytes_AS_STRING(X),
                           PyBytes_GET_SIZE(X));
    }
};

// release GIL
struct PyUnlock
{
    PyThreadState *state;
    PyUnlock() :state(PyEval_SaveThread()) {}
    ~PyUnlock() { PyEval_RestoreThread(state); }
};

// acquire GIL
struct PyLock
{
    PyGILState_STATE state;
    PyLock() :state(PyGILState_Ensure()) {}
    ~PyLock() { PyGILState_Release(state); }
};

// helper when a PyRef may be free'd outside python code
// beware of lock ordering wrt. the GIL
struct PyExternalRef {
    PyRef ref;
    PyExternalRef() {}
    ~PyExternalRef() {
        if(ref) {
            PyLock G;
            ref.reset();
        }
    }
    void swap(PyRef& o) {
        ref.swap(o);
    }
    void swap(PyExternalRef& o) {
        ref.swap(o.ref);
    }
};

#define CATCH() catch(std::exception& e) { if(!PyErr_Occurred()) { PyErr_SetString(PyExc_RuntimeError, e.what()); } }

#if 0
#define TRACE(ARG) do{ std::cerr<<"TRACE "<<__FUNCTION__<<" "<<ARG<<"\n";} while(0)
#else
#define TRACE(ARG) do{ } while(0)
#endif

#if PY_MAJOR_VERSION >= 3
# define MODINIT_RET(VAL) return (VAL)
#else
# define MODINIT_RET(VAL) do {(void)(VAL); return; }while(0)
#endif

#if PY_MAJOR_VERSION >= 3
#define PyMOD(NAME) PyObject* PyInit_##NAME (void)
#else
#define PyMOD(NAME) void init##NAME (void)
#endif

#if PY_MAJOR_VERSION < 3
// quiet some warnings about implict const char* -> char* cast
// for API functions.  These are corrected in py >= 3.x
#define PyObject_CallFunction(O, FMT, ...) PyObject_CallFunction(O, (char*)(FMT), __VA_ARGS__)
#define PyObject_CallMethod(O, METH, FMT, ...) PyObject_CallMethod(O, (char*)(METH), (char*)(FMT), __VA_ARGS__)
#endif

void p4p_type_register(PyObject *mod);
void p4p_value_register(PyObject *mod);
void p4p_server_register(PyObject *mod);
void p4p_array_register(PyObject *mod);
void p4p_client_context_register(PyObject *mod);
void p4p_client_channel_register(PyObject *mod);
void p4p_client_monitor_register(PyObject *mod);
void p4p_client_op_register(PyObject *mod);

epics::pvAccess::ChannelProvider::shared_pointer p4p_build_provider(PyRef &handler, const std::string& name);
PyObject* p4p_add_provider(PyObject *junk, PyObject *args, PyObject *kwds);
PyObject* p4p_remove_provider(PyObject *junk, PyObject *args, PyObject *kwds);
PyObject* p4p_remove_all(PyObject *junk, PyObject *args, PyObject *kwds);
void p4p_server_provider_register(PyObject *mod);

extern PyTypeObject* P4PType_type;
// Extract Structure from P4PType
PyObject* P4PType_wrap(PyTypeObject *type, const epics::pvData::Structure::const_shared_pointer &);
epics::pvData::Structure::const_shared_pointer P4PType_unwrap(PyObject *);
// Find a Field capable of storing the provided value
epics::pvData::Field::const_shared_pointer P4PType_guess(PyObject *);

typedef epics::pvData::shared_vector<const void> array_type;
extern PyTypeObject* P4PArray_type;
PyObject* P4PArray_make(const array_type& v);
const array_type& P4PArray_extract(PyObject* o);

extern PyTypeObject* P4PValue_type;
epics::pvData::PVStructure::shared_pointer P4PValue_unwrap(PyObject *, epics::pvData::BitSet* =0);
std::tr1::shared_ptr<epics::pvData::BitSet> P4PValue_unwrap_bitset(PyObject *);
PyObject *P4PValue_wrap(PyTypeObject *type,
                        const epics::pvData::PVStructure::shared_pointer&,
                        const epics::pvData::BitSet::shared_pointer& = epics::pvData::BitSet::shared_pointer());

extern PyObject* P4PCancelled;

template<class C>
struct PyClassWrapper {
    PyObject_HEAD

    PyObject *weak;

    typedef C value_type;
    typedef C* pointer_type;
    typedef C& reference_type;
    C I;

    static PyTypeObject type;
    static void buildType() {
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_new = &tp_new;
        type.tp_dealloc = &tp_dealloc;

        type.tp_weaklistoffset = offsetof(PyClassWrapper, weak);
    }

    static PyObject* tp_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
        try {
            // we use python alloc instead of new here so that we could participate in GC
            PyRef self(type->tp_alloc(type, 0));
            PyClassWrapper *SELF = (PyClassWrapper*)self.get();

            SELF->weak = NULL;

            // The following can zero out the PyObject_HEAD members
            //new (self.get()) P4PType();
            // instead we only C++ initialize the sub-struct C
            new (&SELF->I) C();

            TRACE("tp_new "<<type->tp_name);

            return self.release();
        } CATCH()
        return NULL;

    }

    static void tp_dealloc(PyObject *raw) {
        PyClassWrapper *self = (PyClassWrapper*)raw;
        TRACE("tp_dealloc "<<Py_TYPE(raw)->tp_name);
        if(self->weak)
            PyObject_ClearWeakRefs(raw);
        PyTypeObject *klass = Py_TYPE(raw);
        if(klass->tp_clear)
            (klass->tp_clear)(raw);
        try {
            self->I.~C();
        } CATCH()
        Py_TYPE(self)->tp_free((PyObject*)self);
    }

    static C& unwrap(PyObject *obj) {
        if(!PyObject_TypeCheck(obj, &type))
            throw std::runtime_error("Unable to unwrap, wrong type");
        PyClassWrapper *W = (PyClassWrapper*)obj;
        return W->I;
    }

    static PyObject *wrap(C* self) {
        return (PyObject*)((char*)self - offsetof(PyClassWrapper, I));
    }
};


#endif // P4P_H
