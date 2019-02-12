# This file can be to include almost all the Fuchsia source files. See [editors.md](./editors.md)
# for usage.
cmake_minimum_required(VERSION 3.9)
project(fuchsia)

set(CMAKE_CXX_STANDARD 17)

macro(get_dirs)
    file(GLOB_RECURSE ALL_DIRS LIST_DIRECTORIES true
            ${PROJECT_SOURCE_DIR}/out/*
            ${PROJECT_SOURCE_DIR}/third_party/*
            ${PROJECT_SOURCE_DIR}/zircon/*
            ${PROJECT_SOURCE_DIR}/garnet/*
            ${PROJECT_SOURCE_DIR}/peridot/*
            ${PROJECT_SOURCE_DIR}/topaz/*)
    set(INCLUDE_DIRS "")
    foreach(dir IN LISTS ALL_DIRS)
        if(${dir} MATCHES "\/include$")
            message("adding ${dir}")
            set(INCLUDE_DIRS ${INCLUDE_DIRS} ${dir})
        endif()
    endforeach()
endmacro()

get_dirs()
LIST(LENGTH INCLUDE_DIRS n_dirs)
message("${n_dirs} directories added")

include_directories(
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/zircon/system/public
        ${PROJECT_SOURCE_DIR}/out/x64/fidling/gen/
        ${PROJECT_SOURCE_DIR}/out/x64/gen
        ${PROJECT_SOURCE_DIR}/garnet/public
        ${PROJECT_SOURCE_DIR}/peridot/public
        ${PROJECT_SOURCE_DIR}/sdk
)

include_directories(${INCLUDE_DIRS})

file(GLOB_RECURSE SRC
        ${PROJECT_SOURCE_DIR}/*.h
        ${PROJECT_SOURCE_DIR}/*.hh
        ${PROJECT_SOURCE_DIR}/*.hpp
        ${PROJECT_SOURCE_DIR}/*.c
        ${PROJECT_SOURCE_DIR}/*.cc
        ${PROJECT_SOURCE_DIR}/*.cpp)

add_executable(fuchsia ${SRC})

add_compile_definitions(__Fuchsia__)
