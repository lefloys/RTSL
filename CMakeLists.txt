cmake_minimum_required(VERSION 3.20)

project(rtsl LANGUAGES C CXX)

set(RTSL_LIBRARY_SOURCES
    src/Basic/Diagnostics.cpp
    src/Basic/SourceManager.cpp
    src/Lex/Lexer.cpp
    src/Parse/Parser.cpp
    src/Sema/Sema.cpp
    src/IR/IR.cpp
    src/IR/UniformLowering.cpp
    src/IR/StageBuiltins.cpp
    src/Mangle/Mangler.cpp
    src/Serialization/Artifact.cpp
    src/Serialization/TextRTIR.cpp
    src/Backend/GlslBackend.cpp
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

# End-to-end check: compile a representative shader and run the emitted GLSL
# through glslangValidator so that an undeclared identifier, GLSL reserved
# word, or invalid layout in the backend output fails CI rather than silently
# producing broken .vert/.frag files. Skipped when glslangValidator is not on
# PATH (e.g. machines without the Vulkan SDK).
find_program(GLSLANG_VALIDATOR glslangValidator)
if(GLSLANG_VALIDATOR)
    set(_e2e_out ${CMAKE_CURRENT_BINARY_DIR}/glsl-e2e)
    add_test(NAME glsl-e2e
        COMMAND ${CMAKE_COMMAND}
            -DRTSLC=$<TARGET_FILE:rtslc>
            -DGLSLANG=${GLSLANG_VALIDATOR}
            -DSHADER=${CMAKE_CURRENT_SOURCE_DIR}/workspace/graphics.rtsl
            -DOUTDIR=${_e2e_out}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunGlslE2E.cmake)
endif()
