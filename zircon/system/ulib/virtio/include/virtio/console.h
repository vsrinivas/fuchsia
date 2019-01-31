// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/compiler.h>

// clang-format off

#define VIRTIO_CONSOLE_F_SIZE               (1u << 0)
#define VIRTIO_CONSOLE_F_MULTIPORT          (1u << 1)
#define VIRTIO_CONSOLE_F_EMERG_WRITE        (1u << 2)

// clang-format on

__BEGIN_CDECLS;

typedef struct virtio_console_config {
  uint16_t cols;
  uint16_t rows;
  uint32_t max_nr_ports;
  uint32_t emerg_wr;
} __PACKED virtio_console_config_t;

__END_CDECLS;
