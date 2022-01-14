# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

virtio_magma_h = custom_target(
  'virtio_magma.h',
  input : magma_json,
  output: 'virtio_magma.h',
  command: [
    prog_python, files('virtio_magma_h_gen.py'), 'linux',
    '@INPUT@', '@OUTPUT@'
  ]
)
