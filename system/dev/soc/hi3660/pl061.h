// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-device.h>
#include <magenta/listnode.h>
#include <magenta/types.h>
#include <threads.h>

typedef struct {
    list_node_t node;
    mtx_t lock;
    pdev_mmio_buffer_t buffer;
    uint32_t gpio_start;
    uint32_t gpio_count;
    const uint32_t* irqs;
    uint32_t irq_count;
} pl061_gpios_t;

// PL061 GPIO protocol ops uses pl061_gpios_t* for ctx
extern gpio_protocol_ops_t pl061_proto_ops;
