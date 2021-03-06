set_source_files_properties(test_value.cc PROPERTIES
    COMPILE_FLAGS -fno-strict-aliasing)
set_source_files_properties(test_containers.cc PROPERTIES
    COMPILE_FLAGS -fno-strict-aliasing)

CONFIGURE_FILE(testsuite.hh.in ${CMAKE_BINARY_DIR}/test/testsuite.hh @ONLY)
ADD_EXECUTABLE(typelib_testsuite testsuite.cc
    test_registry.cc test_value_ops_init.cc
    test_display.cc test_lang_tlb.cc test_model.cc
    test_plugin.cc test_value.cc test_containers.cc
    test_memory_layout.cc test_marshalling.cc test_typebuilder.cc)

TARGET_LINK_LIBRARIES(typelib_testsuite typeLib typeLang_cSupport ${Boost_UNIT_TEST_FRAMEWORK_LIBRARIES} ${Boost_SYSTEM_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
if (ENABLE_INTERNAL_CPARSER)
    TARGET_LINK_LIBRARIES(typelib_testsuite typeLang_c)
endif()

if (ROCK_TEST_LOG_DIR)
    if (NOT ROCK_TEST_BOOST_FORMAT)
        set(ROCK_TEST_BOOST_FORMAT XML)
    endif()

    list(APPEND __boost_test_parameters
        --log_format=${ROCK_TEST_BOOST_FORMAT}
        --log_level=all
        --log_sink=${ROCK_TEST_LOG_DIR}/typelib_testsuite.boost.xml)
    file(MAKE_DIRECTORY "${ROCK_TEST_LOG_DIR}")
endif()

ADD_TEST(CxxSuiteInstalledPlugins ${CMAKE_CURRENT_BINARY_DIR}/typelib_testsuite)
ADD_TEST(CxxSuiteLocalPlugins ${CMAKE_CURRENT_BINARY_DIR}/typelib_testsuite
    ${__boost_test_parameters})
# the pluginmanager can load plugins from a list of paths set in the
# environment. for testing we don't want to use the plugins in the
# install-folder, but the freshly compiled ones in the build-folder. note that
# the folders set in the env-var are hardcoded...
set_property(TEST CxxSuiteLocalPlugins APPEND PROPERTY ENVIRONMENT
    "TYPELIB_PLUGIN_PATH=${CMAKE_BINARY_DIR}/lang/csupport:${CMAKE_BINARY_DIR}/lang/tlb:${CMAKE_BINARY_DIR}/lang/idl")
