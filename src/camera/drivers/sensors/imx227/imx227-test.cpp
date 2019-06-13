// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "imx227-test.h"
#include "imx227.h"

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>

namespace camera {

namespace {

constexpr uint32_t kValidSensorMode = 0;

}

Imx227Device* g_sensor_device;

void Imx227DeviceTester::SetUp() {
    ASSERT_NOT_NULL(g_sensor_device);
    EXPECT_EQ(ZX_OK, g_sensor_device->CameraSensorInit());
}

void Imx227DeviceTester::TearDown() {
    g_sensor_device->CameraSensorDeInit();
}

TEST_F(Imx227DeviceTester, TestSetMode) {
    EXPECT_EQ(ZX_OK, g_sensor_device->CameraSensorSetMode(kValidSensorMode));
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, g_sensor_device->CameraSensorSetMode(0xFF));
}

TEST_F(Imx227DeviceTester, TestStreaming) {
    EXPECT_EQ(ZX_OK, g_sensor_device->CameraSensorStartStreaming());
    // TODO(braval): Figure out a way to validate starting & stopping of streaming
    EXPECT_EQ(ZX_OK, g_sensor_device->CameraSensorStopStreaming());
}

TEST_F(Imx227DeviceTester, TestDeInitState) {
    TearDown();
    EXPECT_NE(ZX_OK, g_sensor_device->CameraSensorSetMode(kValidSensorMode));
    EXPECT_NE(ZX_OK, g_sensor_device->CameraSensorStartStreaming());
    EXPECT_NE(ZX_OK, g_sensor_device->CameraSensorStopStreaming());
}

TEST_F(Imx227DeviceTester, TestStreamingOff) {
    EXPECT_NE(ZX_OK, g_sensor_device->CameraSensorStopStreaming());
}

TEST_F(Imx227DeviceTester, TestStreamingOn) {
    EXPECT_EQ(ZX_OK, g_sensor_device->CameraSensorStartStreaming());
    EXPECT_NE(ZX_OK, g_sensor_device->CameraSensorStartStreaming());
}

zx_status_t Imx227DeviceTester::RunTests(Imx227Device* sensor) {
    g_sensor_device = sensor;
    const int kArgc = 1;
    const char* argv[kArgc] = {"imx227-test"};
    return RUN_ALL_TESTS(kArgc, const_cast<char**>(argv));
}

} // namespace camera
