# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

c_args = []
cpp_args = [ '-DMAGMA_DEBUG_INTERNAL_USE_ONLY' ]

prog_python = import('python').find_installation('python3')
magma_json = files('include/magma/magma.json')

inc_configuration = include_directories(['.', 'src'])
inc_magma = include_directories('include')
inc_platform = include_directories('src/magma_util/platform')
inc_magma_util = include_directories('src/magma_util')
inc_msd_intel_gen = include_directories('../../drivers/msd-intel-gen/include')

# Generate virtio_magma.h in the proper location
subdir('include/virtio')

magma_manual = 'src/libmagma_linux/magma.cc'

magma_generic_cc = custom_target(
  'magma_generic.cc',
  input : ['src/libmagma_linux/magma_generic_cc_gen.py', magma_json, magma_manual],
  output: 'magma_generic.cc',
  command: [prog_python, '@INPUT@', '@OUTPUT@']
)

files_libmagma_linux = files(
  magma_manual,
  'src/magma_util/platform/linux/linux_platform_logger.cc',
)

thread_dep = dependency('threads')

libmagma_linux = static_library(
  'magma_linux',
  sources: [files_libmagma_linux, magma_generic_cc, virtio_magma_h],
  cpp_args: cpp_args,
  c_args: c_args,
  include_directories : [inc_top, inc_configuration, inc_magma,
                         inc_platform, inc_magma_util],
  dependencies: thread_dep
)

dep_magma = declare_dependency(
  include_directories : [inc_magma, inc_msd_intel_gen],
  link_with : libmagma_linux)

if get_option('with_tests')
  subdir('tests')
endif
