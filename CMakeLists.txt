cmake_minimum_required(VERSION 3.20)

project(rtsl LANGUAGES C CXX)

set(RTSL_LIBRARY_SOURCES
    src/Basic/Diagnostics.cpp
    src/Basic/SourceManager.cpp
    src/Lex/Lexer.cpp
    src/Parse/Parser.cpp
    src/Sema/Sema.cpp
    src/IR/IR.cpp
    src/Mangle/Mangler.cpp
    src/Serialization/Artifact.cpp
    src/Serialization/TextRTIR.cpp
    src/Compiler/Compiler.cpp
    src/Link/Linker.cpp
    src/rtsl.cpp
)

add_library(rtsl STATIC ${RTSL_LIBRARY_SOURCES})

target_compile_features(rtsl PUBLIC cxx_std_23)

target_include_directories(rtsl
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(rtsl PROPERTIES
    CXX_EXTENSIONS OFF
)

add_executable(rtslc src/Driver/rtslc.cpp)

target_link_libraries(rtslc PRIVATE rtsl)

target_compile_features(rtslc PRIVATE cxx_std_23)

target_include_directories(rtslc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

set_target_properties(rtslc PROPERTIES
    CXX_EXTENSIONS OFF
)

add_executable(rtsl-tests tests/rtsl_tests.cpp)

target_link_libraries(rtsl-tests PRIVATE rtsl)

target_compile_features(rtsl-tests PRIVATE cxx_std_23)

target_include_directories(rtsl-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

set_target_properties(rtsl-tests PROPERTIES
    CXX_EXTENSIONS OFF
)

enable_testing()
add_test(NAME rtsl-tests COMMAND rtsl-tests)
