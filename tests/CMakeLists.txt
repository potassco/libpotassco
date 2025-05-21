add_executable(test_potassco_lp)
target_sources(test_potassco_lp PRIVATE
    test_aspif.cpp
    test_error.cpp
    test_smodels.cpp
    test_text.cpp)
add_executable(test_potassco_opts)
target_sources(test_potassco_opts PRIVATE
    test_application.cpp
    test_options.cpp
    test_string_convert.cpp)

foreach(target test_potassco_lp test_potassco_opts)
    target_link_libraries(${target} PRIVATE libpotassco Catch2::Catch2WithMain potassco_default_warnings)
    target_include_directories(${target} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
    set_target_properties(${target} PROPERTIES FOLDER test)
endforeach()

catch_discover_tests(test_potassco_lp PROPERTIES TIMEOUT 10)
catch_discover_tests(test_potassco_opts PROPERTIES TIMEOUT 10)
