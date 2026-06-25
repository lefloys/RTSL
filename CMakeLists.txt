cmake_minimum_required(VERSION 3.20)

project(rtsl LANGUAGES C CXX)

add_library(rtsl STATIC)

target_compile_features(rtsl PUBLIC cxx_std_17)

target_include_directories(rtsl
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_sources(rtsl
    PRIVATE
        src/rtsl.cpp
)

set_target_properties(rtsl PROPERTIES
    CXX_EXTENSIONS OFF
)

add_executable(rtslc)

target_sources(rtslc
    PRIVATE
        src/rtslc.cpp
)

target_link_libraries(rtslc
    PRIVATE
        rtsl
)

target_compile_features(rtslc PRIVATE cxx_std_17)

set_target_properties(rtslc PROPERTIES
    CXX_EXTENSIONS OFF
)
