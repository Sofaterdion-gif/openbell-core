cmake_minimum_required(VERSION 3.16)
project(openbell-core VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------- library
# scenario.hpp is header-only (no compilation unit needed); engine + pricing
# build into a small static library.
add_library(openbell-core STATIC
    src/engine.cpp
    src/pricing.cpp
)
add_library(openbell-core::openbell-core ALIAS openbell-core)

target_include_directories(openbell-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(openbell-core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(openbell-core PRIVATE /utf-8)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(openbell-core PRIVATE
        -finput-charset=UTF-8
        -fexec-charset=UTF-8
    )
endif()

# ---------------------------------------------------------------- tests
include(CTest)
if(BUILD_TESTING)
    foreach(t test_engine test_pricing test_scenario)
        add_executable(${t} tests/${t}.cpp)
        target_link_libraries(${t} PRIVATE openbell-core)
        target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        if(MSVC)
            target_compile_options(${t} PRIVATE /utf-8)
        elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${t} PRIVATE
                -finput-charset=UTF-8
                -fexec-charset=UTF-8
            )
        endif()
        add_test(NAME ${t} COMMAND ${t})
    endforeach()
endif()

# ---------------------------------------------------------------- demo
add_executable(openbell-demo examples/demo.cpp)
target_link_libraries(openbell-demo PRIVATE openbell-core)

# ---------------------------------------------------------------- install
include(GNUInstallDirs)
install(TARGETS openbell-core
    EXPORT openbell-core-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT openbell-core-targets
    NAMESPACE openbell-core::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/openbell-core
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/openbell-core-config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/openbell-core-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/openbell-core
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/openbell-core-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/openbell-core-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/openbell-core-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/openbell-core
)
