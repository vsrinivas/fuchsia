// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>

namespace board_as370 {

class As370 : public ddk::Device<As370> {
public:
    As370(zx_device_t* parent) : ddk::Device<As370>(parent) {}

    static zx_status_t Create(void* ctx, zx_device_t* parent);

    void DdkRelease() { delete this; }
};

}  // namespace board_as370
