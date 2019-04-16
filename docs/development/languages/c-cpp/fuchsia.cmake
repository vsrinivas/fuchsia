# This file can be used to include almost all the Fuchsia source files. See
# [editors.md](./editors.md) for usage.
cmake_minimum_required(VERSION 3.9)
project(fuchsia)
set(CMAKE_CXX_STANDARD 17)

# Some header files include additional header files based on `__Fuchsia__`'s presence
# (e.g. https://fuchsia.googlesource.com/garnet/+/master/public/lib/fidl/cpp/internal/header.h).
add_compile_definitions(__Fuchsia__)

# This section adds any sub-directory named "include" with the assumption that it is the root for
# include paths.
file(GLOB_RECURSE ALL_DIRS LIST_DIRECTORIES true
        ${PROJECT_SOURCE_DIR}/garnet/*
        ${PROJECT_SOURCE_DIR}/out/*
        ${PROJECT_SOURCE_DIR}/peridot/*
        ${PROJECT_SOURCE_DIR}/prebuilt/*
        ${PROJECT_SOURCE_DIR}/src/*
        ${PROJECT_SOURCE_DIR}/third_party/*
        ${PROJECT_SOURCE_DIR}/topaz/*
        ${PROJECT_SOURCE_DIR}/vendor/*
        ${PROJECT_SOURCE_DIR}/zircon/*
        )
set(INCLUDE_DIRS "")
foreach(dir IN LISTS ALL_DIRS)
    if(${dir} MATCHES "\/include$")
        message("adding ${dir}")
        set(INCLUDE_DIRS ${INCLUDE_DIRS} ${dir})
    endif()
endforeach()
include_directories(${INCLUDE_DIRS})
LIST(LENGTH INCLUDE_DIRS n_dirs)
message("${n_dirs} directories named 'include' added to include_directories")

# Any other include roots must be manually added here.
include_directories(
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/garnet/public
        ${PROJECT_SOURCE_DIR}/out/x64/fidling/gen/
        ${PROJECT_SOURCE_DIR}/out/x64/gen
        ${PROJECT_SOURCE_DIR}/peridot/public
        ${PROJECT_SOURCE_DIR}/sdk
        ${PROJECT_SOURCE_DIR}/zircon/system/public
        )

# Find all sources files and add them to an executable so that CLion will know to scan them.
file(GLOB_RECURSE SRC
        ${PROJECT_SOURCE_DIR}/*.h
        ${PROJECT_SOURCE_DIR}/*.hh
        ${PROJECT_SOURCE_DIR}/*.hpp
        ${PROJECT_SOURCE_DIR}/*.c
        ${PROJECT_SOURCE_DIR}/*.cc
        ${PROJECT_SOURCE_DIR}/*.cpp
        )

# Work around an issue of dart-pkg having checked in a symlink to a header that itself is not
# checked in. Because the problematic content (for now) is just an on-macOS integration test,
# we should probably just not import that content into the Fuchsia development environment at
# all.
# TODO(BLD-393): Don't import uninteresting-to-Fuchsia parts of the Flutter repository.
set(n_paths, 0)
foreach (TMP_PATH ${SRC})
    if(${TMP_PATH} MATCHES "\/third_party\/dart-pkg")
        message("removing ${TMP_PATH}")
        MATH(EXPR n_paths "${n_paths}+1")
        list (REMOVE_ITEM SRC ${TMP_PATH})
    endif()
endforeach(TMP_PATH)
message("${n_paths} files from `/third_party/dart-pkg/` removed")

add_executable(fuchsia ${SRC})

# TODO(BLD-392): This file should be at least "sanity" (executes without failing) tested.
