#include <ruby.h>
#include <dl.h>
#include <typelib/registry.hh>
#include <typelib/typevisitor.hh>
#include <typelib/value.hh>
#include <typelib/pluginmanager.hh>
#include <typelib/importer.hh>
#include <utilmm/configfile/configset.hh>

using namespace Typelib;
using utilmm::config_set;

static VALUE mTypelib   = Qnil;
static VALUE cValue     = Qnil;
static VALUE cRegistry  = Qnil;
static VALUE cType      = Qnil;

// Never destroy a type. The Type objects are destroyed with
// the registry they belong
static void do_not_delete(void*) {}

static
Value* rb_value2cxx(VALUE self)
{
    Value* value = 0;
    Data_Get_Struct(self, Value, value);
    return value;
}
static
Registry* rb_registry2cxx(VALUE self)
{
    Registry* registry = 0;
    Data_Get_Struct(self, Registry, registry);
    return registry;
}

static
void value_delete(void* self) { delete reinterpret_cast<Value*>(self); }

static
VALUE value_alloc(VALUE klass)
{ return Data_Wrap_Struct(klass, 0, value_delete, new Value); }


/** This visitor takes a Value class and a field name,
 *  and returns the VALUE object which corresponds to
 *  the field, or returns nil
 */
class RubyGetter : public ValueVisitor
{
    VALUE m_value;

    virtual bool visit_ (int8_t  & value) { m_value = CHR2FIX(value); return false; }
    virtual bool visit_ (uint8_t & value) { m_value = CHR2FIX(value); return false; }
    virtual bool visit_ (int16_t & value) { m_value = INT2NUM(value); return false; }
    virtual bool visit_ (uint16_t& value) { m_value = INT2NUM(value); return false; }
    virtual bool visit_ (int32_t & value) { m_value = INT2NUM(value); return false; }
    virtual bool visit_ (uint32_t& value) { m_value = INT2NUM(value); return false; }
    virtual bool visit_ (int64_t & value) { m_value = LL2NUM(value);  return false; }
    virtual bool visit_ (uint64_t& value) { m_value = ULL2NUM(value); return false; }
    virtual bool visit_ (float   & value) { m_value = rb_float_new(value); return false; }
    virtual bool visit_ (double  & value) { m_value = rb_float_new(value); return false; }

    virtual bool visit_pointer  (Value const&)   { return true; }
    virtual bool visit_array    (Value const& v) { throw UnsupportedType(v.getType()); }
    virtual bool visit_compound (Value const& v)
    { 
        m_value = Data_Wrap_Struct(cValue, 0, value_delete, new Value(v));
        return false; 
    }
    // Shouldn't get in visit_field since visit_compound returns false
    virtual bool visit_field    (Field const&, Value const&) { return false; }
    virtual bool visit_enum     (Value const& v)   { throw UnsupportedType(v.getType()); }
    
public:
    RubyGetter()
        : ValueVisitor(false) {}

    VALUE apply(Value v, VALUE name)
    {
        m_value = Qnil;
        try { 
            Value v = value_get_field(v, StringValuePtr(name));
            ValueVisitor::apply(v);
            return m_value;
        } 
        catch(FieldNotFound) { return Qnil; }
        catch(UnsupportedType) { return Qnil; }
    }
};

class RubySetter : public ValueVisitor
{
    VALUE m_value;

    virtual bool visit_ (int8_t  & value) { value = NUM2CHR(m_value); return false; }
    virtual bool visit_ (uint8_t & value) { value = NUM2CHR(m_value); return false; }
    virtual bool visit_ (int16_t & value) { value = NUM2LONG(m_value); return false; }
    virtual bool visit_ (uint16_t& value) { value = NUM2ULONG(m_value); return false; }
    virtual bool visit_ (int32_t & value) { value = NUM2LONG(m_value); return false; }
    virtual bool visit_ (uint32_t& value) { value = NUM2ULONG(m_value); return false; }
    virtual bool visit_ (int64_t & value) { value = NUM2LL(m_value);  return false; }
    virtual bool visit_ (uint64_t& value) { value = NUM2LL(m_value); return false; }
    virtual bool visit_ (float   & value) { value = NUM2DBL(m_value); return false; }
    virtual bool visit_ (double  & value) { value = NUM2DBL(m_value); return false; }

    virtual bool visit_pointer  (Value const&)   { return true; }
    virtual bool visit_array    (Value const& v) { throw UnsupportedType(v.getType()); }
    virtual bool visit_compound (Value const& v)
    { 
        m_value = Data_Wrap_Struct(cValue, 0, value_delete, new Value(v));
        return false; 
    }
    // Shouldn't get in visit_field since visit_compound returns false
    virtual bool visit_field    (Field const&, Value const&) { return false; }
    virtual bool visit_enum     (Value const& v)   { throw UnsupportedType(v.getType()); }
    
public:
    RubySetter()
        : ValueVisitor(false) {}

    bool apply(Value v, VALUE name, VALUE new_value)
    {
        m_value = new_value;
        try { 
            Value v = value_get_field(v, StringValuePtr(name));
            ValueVisitor::apply(v);
            return true;
        } 
        catch(FieldNotFound) { return false; } 
        catch(UnsupportedType) { return false; }
    }
};

static
VALUE value_initialize(VALUE self, VALUE ptr, VALUE type)
{
    Value* value = rb_value2cxx(self);

    // Protect 'type' against the GC
    rb_iv_set(self, "@type", type);
    Type const* t;
    Data_Get_Struct(type, Type, t);

    if(NIL_P(ptr))
        ptr = rb_dlptr_malloc(t->getSize(), free);

    // Protect 'ptr' against the GC
    ptr = rb_funcall(ptr, rb_intern("to_ptr"), 1, type);
    rb_iv_set(self, "@ptr", ptr);

    *value = Value(rb_dlptr2cptr(ptr), *t);
    return self;
}

// TODO: check if the given field is settable/gettable
// for now, we only check its existence
static
VALUE value_respond_to_p(VALUE self, VALUE id)
{
    try { 
        Value* v = rb_value2cxx(self);

        value_get_field(*v, StringValuePtr(id));
        return Qtrue;
    } catch(FieldNotFound) { 
        return rb_call_super(1, &id);
    }
}

static
VALUE value_method_missing(VALUE self, int argc, VALUE* argv)
{
    Value* v = rb_value2cxx(self);

    if (argc == 1)
    {
        RubyGetter getter;
        VALUE ruby_value = getter.apply(*v, argv[0]);
        if (NIL_P(ruby_value))
            return rb_call_super(argc, argv);
        return ruby_value;
    }
    else if (argc == 2)
    {
        RubySetter setter;
        if (!setter.apply(*v, argv[0], argv[1]))
            return rb_call_super(argc, argv);
        return argv[1];
    }
    else
        return rb_call_super(argc, argv);

    // never reached
    return Qnil;
}

static 
void registry_free(void* ptr) { delete reinterpret_cast<Registry*>(ptr); }

static
VALUE registry_alloc(VALUE klass)
{
    Registry* registry = new Registry;
    return Data_Wrap_Struct(klass, 0, registry_free, registry);
}

static
VALUE registry_get(VALUE self, VALUE name)
{
    Registry* registry = rb_registry2cxx(self);
    Type const* type = registry->get( StringValuePtr(name) );

    if (! type)
        return Qnil;

    VALUE rtype = Data_Wrap_Struct(cType, 0, do_not_delete, const_cast<Type*>(type));
    // Set a registry attribute to keep the registry alive
    rb_iv_set(rtype, "@registry", self);

    return rtype;
}

/* Private method to import a given file in the registry
 * We expect Registry#import to format the arguments before calling
 * do_import
 */
static
VALUE registry_import(VALUE self, VALUE file, VALUE kind, VALUE options)
{
    Registry* registry = rb_registry2cxx(self);
    
    PluginManager::self manager;
    Importer* importer = manager->importer( StringValuePtr(kind) );

    config_set config;
    if (! NIL_P(options))
    {
        for (int i = 0; i < RARRAY(options)->len; ++i)
        {
            VALUE entry = RARRAY(options)->ptr[i];
            VALUE k = RARRAY(entry)->ptr[0];
            VALUE v = RARRAY(entry)->ptr[1];

            if ( TYPE(v) == T_ARRAY )
            {
                for (int j = 0; j < RARRAY(v)->len; ++j)
                    config.insert(StringValuePtr(k), StringValuePtr( RARRAY(v)->ptr[j] ));
            }
            else
                config.set(StringValuePtr(k), StringValuePtr(v));
        }
    }
        
    // TODO: error checking
    importer->load(StringValuePtr(file), config, *registry);
    return Qnil;
}

extern "C" void Init_typelib_api()
{
    mTypelib  = rb_define_module("Typelib");
    
    cValue    = rb_define_class_under(mTypelib, "Value", rb_cObject);
    rb_define_alloc_func(cValue, value_alloc);
    rb_define_method(cValue, "initialize", (VALUE (*)(...))value_initialize, 2);
    rb_define_method(cValue, "method_missing", (VALUE (*)(...))value_method_missing, -1);
    rb_define_method(cValue, "respond_to?", (VALUE (*)(...))value_respond_to_p, 1);
    
    cRegistry = rb_define_class_under(mTypelib, "Registry", rb_cObject);
    rb_define_alloc_func(cRegistry, registry_alloc);
    rb_define_method(cRegistry, "get", (VALUE (*)(...))registry_get, 1);
    // do_import is called by the Ruby-defined import, which formats the 
    // option hash (if there is one) and can detect the import type by extension
    rb_define_method(cRegistry, "do_import", (VALUE (*)(...))registry_import, 3);

    cType     = rb_define_class_under(mTypelib, "Type", rb_cObject);
}

