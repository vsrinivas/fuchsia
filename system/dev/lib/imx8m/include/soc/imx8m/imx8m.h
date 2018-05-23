// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <zircon/listnode.h>

typedef struct {
    io_buffer_t iomuxc_base;
} imx8m_t;

zx_status_t imx8m_init(zx_handle_t resource, zx_handle_t bti, imx8m_t** out);
