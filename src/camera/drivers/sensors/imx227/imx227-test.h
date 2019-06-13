// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <memory>
#include <zxtest/zxtest.h>

namespace camera {
// |Imx227Devicetester| is spawned by the driver in |imx227.cpp|
class Imx227Device;

class Imx227DeviceTester : public zxtest::Test {
public:
    static zx_status_t RunTests(Imx227Device* device);

protected:
    // Setup & TearDown
    void SetUp();
    void TearDown();
};

} // namespace camera
