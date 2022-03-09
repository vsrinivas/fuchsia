# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file should be linked into the toplevel fuchsia dir.

project('fuchsia', 'cpp', 'c',
         version : '0.0.1',
         license : 'MIT OR Apache-2.0',
         default_options: 'cpp_std=c++17')

inc_top=include_directories('.')

if get_option('with_gfxstream')
    libgfxstream_proj = subproject('goldfish-opengl')

    # goldfish-opengl doesn't publish a dependency, so we make one here
    lib_codec_common = libgfxstream_proj.get_variable('lib_codec_common')
    inc_opengl_codec = libgfxstream_proj.get_variable('inc_opengl_codec')

    inc_android_emu = libgfxstream_proj.get_variable('inc_android_emu')
    lib_emu_android_base = libgfxstream_proj.get_variable('lib_emu_android_base')

    inc_goldfish_address_space = libgfxstream_proj.get_variable('inc_goldfish_address_space')
    lib_goldfish_address_space = libgfxstream_proj.get_variable('lib_goldfish_address_space')

    inc_stream = libgfxstream_proj.get_variable('inc_opengl_system')
    lib_stream = libgfxstream_proj.get_variable('lib_stream')

    inc_host = libgfxstream_proj.get_variable('inc_host')
    inc_system = libgfxstream_proj.get_variable('inc_system')

    lib_android_compat = libgfxstream_proj.get_variable('lib_android_compat')

    gfxstream_dep = declare_dependency(
        include_directories : [
            inc_host,
            inc_opengl_codec,
            inc_stream,
            inc_android_emu,
            inc_goldfish_address_space,
            inc_system,
        ],
        link_with : [
            lib_stream,
            lib_goldfish_address_space,
            lib_emu_android_base,
            lib_codec_common,
            lib_android_compat,
        ],
        compile_args : [ '-DVIRTIO_GPU' ]
    )
endif

subdir('src/graphics/lib/magma')
