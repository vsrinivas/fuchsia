// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/protocol/clk.h>
#include <fbl/unique_ptr.h>
#include <ddk/protocol/platform-defs.h>

namespace thermal {
// This class handles the dynamic changing of
// CPU frequency.
class AmlCpuFrequency {

public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlCpuFrequency);
    AmlCpuFrequency(){};
    zx_status_t Init(zx_device_t* parent);

private:
    // Protocols.
    clk_protocol_t clk_protocol_;
    fbl::unique_ptr<ddk::ClkProtocolProxy> clk_;
};
} // namespace thermal
