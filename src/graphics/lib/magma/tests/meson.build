# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Debian packages needed:
#   -'rapidjson-dev'
#   -'googletest'
#   -'libdrm-dev'
dl_dep = meson.get_compiler('cpp').find_library('dl', required: false)
gtest_dep = dependency('gtest', main: true)
drm_dep = dependency('libdrm')

foreach test : ['virtmagma_unit_tests', 'virtmagma_image_tests']
  files_unit_test = files(
    '../tests_linux/unit_tests/@0@.cc'.format(test)
  )

  test_exe = executable(
    test,
    sources: files_unit_test,
    include_directories : [inc_configuration, inc_magma, inc_virtio,
                        inc_platform],
    cpp_args: cpp_args,
    link_with : [libmagma_linux],
    dependencies : [gtest_dep, dl_dep, drm_dep],
    install : false,
  )

  test(test, test_exe)
endforeach

test = 'virtmagma_conformance_tests'

files_test = files(
  'integration/test_magma.cc',
  'integration/test_magma_c.c',
  'helper/magma_map_cpu.cc',
)

test_magma = executable(
  test,
  sources: files_test,
  include_directories : [inc_configuration, inc_top, inc_magma, inc_msd_intel_gen, inc_virtio,
                         inc_platform],
  cpp_args: cpp_args + ['-DVIRTMAGMA=1'],
  link_with : [libmagma_linux],
  dependencies : [gtest_dep],
  install : false,
)

test(test, test_magma)
