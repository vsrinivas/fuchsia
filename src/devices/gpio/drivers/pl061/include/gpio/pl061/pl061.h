// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_GPIO_DRIVERS_PL061_INCLUDE_GPIO_PL061_PL061_H_
#define SRC_DEVICES_GPIO_DRIVERS_PL061_INCLUDE_GPIO_PL061_PL061_H_

#include <threads.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <ddk/mmio-buffer.h>
#include <ddk/protocol/gpioimpl.h>
#include <ddk/protocol/platform/device.h>

typedef struct {
  list_node_t node;
  mtx_t lock;
  mmio_buffer_t buffer;
  uint32_t gpio_start;
  uint32_t gpio_count;
  const uint32_t* irqs;
  uint32_t irq_count;
} pl061_gpios_t;

// PL061 GPIO protocol ops uses pl061_gpios_t* for ctx
extern gpio_impl_protocol_ops_t pl061_proto_ops;

#endif  // SRC_DEVICES_GPIO_DRIVERS_PL061_INCLUDE_GPIO_PL061_PL061_H_
