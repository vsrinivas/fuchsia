// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <memory>
#include <zxtest/zxtest.h>

namespace gdc {
// |GdcDeviceTester| is spawned by the driver in |gdc.cpp|
class GdcDevice;

class GdcDeviceTester : public zxtest::Test {
public:
    static zx_status_t RunTests(GdcDevice* device);

protected:
    // Setup & TearDown
    void SetUp();
    void TearDown();
};

} // namespace gdc
