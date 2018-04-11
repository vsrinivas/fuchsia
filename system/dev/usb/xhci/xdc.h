// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>

// TODO(jocelyndang): we should get our own handles rather than borrowing them from XHCI.
zx_status_t xdc_bind(zx_device_t* parent, zx_handle_t bti_handle, void* mmio);