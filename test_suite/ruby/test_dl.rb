require 'test_config'
require 'typelib'
require 'test/unit'
require '.libs/test_rb_value'
require 'pp'

class TC_DL < Test::Unit::TestCase
    include Typelib
    
    attr_reader :lib, :registry
    def setup
        @registry = Registry.new
        testfile = File.join(SRCDIR, "test_cimport.1")
        assert_raises(RuntimeError) { registry.import( testfile  ) }
        registry.import( testfile, "c" )

        @lib = Typelib.dlopen('.libs/test_rb_value.so', registry)
    end

    def test_wrapping
        wrapper = lib.wrap('test_simple_function_wrapping', "int", "int", "short")
        assert_equal(1, wrapper[1, 2])

        wrapper = lib.wrap('test_ptr_passing', "int", "struct A*")
        a = Value.new(nil, registry.get("struct A"))
        set_struct_A_value(a)
        assert_equal(1, wrapper[a.to_ptr])

        # Now, test simple type coercion
        assert_equal(1, wrapper[a])

        wrapper = lib.wrap('test_ptr_return', 'struct A*')
        a = wrapper.call
        assert_equal(10, a.a)
        assert_equal(20, a.b)
        assert_equal(30, a.c)
        assert_equal(40, a.d)

    end

    def test_ptr_changes
        # Check that parameters passed by pointer are changed
        wrapper = lib.wrap('test_ptr_argument_changes', nil, 'struct B*')
        b = Value.new(nil, registry.get("struct B"))
        wrapper[b]
        check_B_c_value(b)
    end

    def test_validation
        # Check that nil is not allowed anywhere but at the return value
        assert_raises(ArgumentError) { lib.wrap('test_simple_function_wrapping', "int", nil, "short") }
        assert_nothing_raised { lib.wrap('test_ptr_argument_changes', nil, 'struct B*') }
        GC.start

        # Check that plain structures aren't allowed
        assert_raises(ArgumentError) { lib.wrap('test_simple_function_wrapping', "int", "struct A") }

        a = Value.new(nil, registry.get("struct A"))
        b = Value.new(nil, registry.get("struct B"))
        # Check that pointers are properly typechecked
        wrapper = lib.wrap('test_ptr_return', 'struct A*')
        assert_raises(ArgumentError) { wrapper[b] }
    end

    def test_argument_output
        wrapper = lib.wrap('test_ptr_argument_changes', [nil, 1], 'struct B*')
        b = wrapper.call
        check_B_c_value(b)

        wrapper = lib.wrap('test_arg_input_output', [nil, -1], 'int*', 'INPUT_OUTPUT_MODE')
        assert_raises(ArgumentError) { wrapper[:OUTPUT] }
        assert_equal(5, wrapper[0, :OUTPUT])
        assert_equal(5, wrapper[10, :BOTH])

        wrapper = lib.wrap('test_arg_input_output', [nil, 1], 'int*', 'INPUT_OUTPUT_MODE')
        assert_raises(ArgumentError) { wrapper[10, :BOTH] }
        assert_equal(5, wrapper[:OUTPUT])
        
        wrapper = lib.wrap('test_ptr_argument_changes', [nil, -1], 'struct B*')
        b.a = 250;
        new_b = wrapper[b]
        # b and new_b are supposed to be the same objects
        assert_equal(b, new_b)
        check_B_c_value(new_b)

        # Check enum I/O handling
        wrapper = lib.wrap('test_enum_io_handling', [nil, -1], 'INPUT_OUTPUT_MODE*')
        assert_equal(:BOTH, wrapper[:OUTPUT])
        assert_equal(:OUTPUT, wrapper[:BOTH])
    end

    def test_void_return_type
        wrapper = lib.wrap('test_ptr_argument_changes', ["void", 1], 'struct B*')
        b = wrapper.call
        check_B_c_value(b)
    end

    def test_opaque_handling
        wrapper = lib.wrap('test_opaque_handling', "OpaqueType")
        my_handler = wrapper[]
        wrapper = lib.wrap('check_opaque_value', 'int', 'OpaqueType')
        assert_equal(1, wrapper[my_handler])
    end

    def test_string_handling
        wrapper = lib.wrap('test_string_argument', 'int', 'char*')
        assert_equal(1, wrapper["test"])

        wrapper = lib.wrap('test_string_return', 'char*')
        assert_equal("string_return", wrapper[].to_string)

        wrapper = lib.wrap('test_string_argument_modification', 'void', 'char*', 'int')
        buffer = Value.new(nil, lib.registry.build("char[256]"))
        wrapper[buffer, 256]
        assert_equal("string_return", buffer.to_string)
    end
end
