cmake_minimum_required(VERSION 3.16)
project(LIB_POTASSCO VERSION 2.0 LANGUAGES CXX)
if(POLICY CMP0063)
    cmake_policy(SET CMP0063 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)

# enable folders in IDEs like Visual Studio
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
include(GNUInstallDirs)
include(CMakeDependentOption)
option(LIB_POTASSCO_BUILD_TESTS "whether to build tests" OFF)
option(LIB_POTASSCO_BUILD_APP "whether to build lpconvert tool" ON)
option(LIB_POTASSCO_INSTALL_LIB "whether to install libpotassco" OFF)
option(LIB_POTASSCO_INSTALL_VERSIONED "whether to use a versioned install layout" OFF)
cmake_dependent_option(LIB_POTASSCO_TESTS_ENABLE_DBG_HEAP "enable windows debug heap in catch2" OFF "LIB_POTASSCO_BUILD_TESTS" OFF)
cmake_dependent_option(LIB_POTASSCO_BUILD_STATIC "link lpconvert statically if supported" OFF "LIB_POTASSCO_BUILD_APP" ON)

if(LIB_POTASSCO_INSTALL_VERSIONED)
    set(include_dest "potassco-${LIB_POTASSCO_VERSION}")
    set(library_dest "potassco-${LIB_POTASSCO_VERSION}")
    set(cmake_dest "potassco-${LIB_POTASSCO_VERSION}/cmake")
else()
    set(include_dest ".")
    set(library_dest ".")
    set(cmake_dest "cmake/Potassco")
endif()

if(LIB_POTASSCO_INSTALL_LIB AND NOT CMAKE_INSTALL_LIBDIR)
    message(STATUS "LIBDIR no set - using lib")
    set(CMAKE_INSTALL_LIBDIR lib)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type selected - using 'Release'")
    set(CMAKE_BUILD_TYPE "Release")
endif()

if(NOT MSVC)
    if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    endif()
    if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endif()
    if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endif()
else()
    set(VC_RELEASE_LINK_OPTIONS /LTCG)
    SET(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE}    ${VC_RELEASE_LINK_OPTIONS}")
    SET(CMAKE_MODULE_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS_RELEASE} ${VC_RELEASE_LINK_OPTIONS}")
    SET(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} ${VC_RELEASE_LINK_OPTIONS}")
    SET(CMAKE_STATIC_LINKER_FLAGS_RELEASE "${CMAKE_STATIC_LINKER_FLAGS_RELEASE} ${VC_RELEASE_LINK_OPTIONS}")
endif()

add_library(potassco_default_warnings INTERFACE)
add_library(potassco::potassco_default_warnings ALIAS potassco_default_warnings)
if(MSVC)
    set(PROJECT_WARNINGS_CXX /W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(PROJECT_WARNINGS_CXX
        -Wall -Wextra -Wpedantic -Wshadow -Wundef -Wformat -Wformat=2 -Wunused -Wsign-conversion -Wimplicit-fallthrough
        -Wextra-semi -Woverloaded-virtual -Wnon-virtual-dtor)
endif()
target_compile_options(potassco_default_warnings
    INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:${PROJECT_WARNINGS_CXX}>)

add_subdirectory(src)
add_subdirectory(third_party/amc)
if(NOT AMC_PEDANTIC)
    # NOTE: This should be part of the AMC cmake config.
    target_compile_definitions(amc INTERFACE AMC_NONSTD_FEATURES)
endif()

get_directory_property(IS_SUBPROJECT PARENT_DIRECTORY)

if(LIB_POTASSCO_BUILD_TESTS OR IS_SUBPROJECT)
    set(CATCH_BUILD_STATIC_LIBRARY ON CACHE INTERNAL " " FORCE)
    set(CATCH_CONFIG_WINDOWS_CRTDBG ${LIB_POTASSCO_TESTS_ENABLE_DBG_HEAP} CACHE INTERNAL " " FORCE)
    add_subdirectory(third_party/Catch2)
    if(IS_SUBPROJECT)
        set(CMAKE_MODULE_PATH " ${CMAKE_MODULE_PATH}" PARENT_SCOPE)
    endif()
endif()

if(LIB_POTASSCO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(LIB_POTASSCO_BUILD_APP)
    add_subdirectory(app)
endif()

# optional doc target
find_package(Doxygen)
if(DOXYGEN_FOUND)
    set(doxyfile "${CMAKE_CURRENT_SOURCE_DIR}/doc/Doxyfile")
    add_custom_target(doc_potassco
        COMMAND ${DOXYGEN_EXECUTABLE} ${doxyfile}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/doc"
        COMMENT "Generating documentation..."
        VERBATIM)
    set_target_properties(doc_potassco PROPERTIES FOLDER doc)
endif()

# export
if(LIB_POTASSCO_INSTALL_LIB)
    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        ${PROJECT_SOURCE_DIR}/cmake/PotasscoConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/PotasscoConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/${cmake_dest})
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/PotasscoConfigVersion.cmake
        COMPATIBILITY SameMajorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/PotasscoConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/PotasscoConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/${cmake_dest})
    install(EXPORT PotasscoTargets DESTINATION "${CMAKE_INSTALL_LIBDIR}/${cmake_dest}")
endif()
