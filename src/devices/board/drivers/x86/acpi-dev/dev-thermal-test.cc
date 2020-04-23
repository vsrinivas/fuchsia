// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dev-thermal.h"

#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/fidl-async/bind.h>
#include <math.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

#include "methods.h"
#include "util.h"

zx_status_t acpi_crt_call(ACPI_HANDLE dev_obj, uint64_t* out) { return ZX_OK; }

zx_status_t acpi_psv_call(ACPI_HANDLE dev_obj, uint64_t* out) { return ZX_OK; }

static uint64_t acpi_tmp_call_out = 0;
zx_status_t acpi_tmp_call(ACPI_HANDLE dev_obj, uint64_t* out) {
  *out = acpi_tmp_call_out;
  return ZX_OK;
}

ACPI_STATUS acpi_evaluate_method_intarg(ACPI_HANDLE handle, const char* name, uint64_t arg) {
  return AE_OK;
}

ACPI_STATUS acpi_evaluate_integer(ACPI_HANDLE handle, const char* name, uint64_t* out) {
  return AE_OK;
}

namespace acpi_thermal {

// For testing we want to hook into the underlying FIDL ops of the device.
extern const fuchsia_hardware_thermal_Device_ops_t fidl_ops;

struct thermal_test_context {
  acpi_thermal_device_t dev;
  async_loop_t* loop;
  zx_handle_t client;
  thrd_t thread;
};

static void setup(struct thermal_test_context* ctx) {
  ctx->dev.acpi_handle = NULL;
  mtx_init(&ctx->dev.lock, mtx_plain);
  ctx->dev.event = ZX_HANDLE_INVALID;
  ctx->dev.trip_point_count = 0;

  ASSERT_OK(async_loop_create(&kAsyncLoopConfigAttachToCurrentThread, &ctx->loop));

  zx_handle_t server;
  ASSERT_OK(zx_channel_create(0, &ctx->client, &server));

  ASSERT_OK(fidl_bind(async_loop_get_dispatcher(ctx->loop), server,
                      (fidl_dispatch_t*)fuchsia_hardware_thermal_Device_dispatch, &ctx->dev,
                      &fidl_ops));

  ASSERT_OK(async_loop_start_thread(ctx->loop, "x86-thermal-test-loop", &ctx->thread));
}

static void teardown(struct thermal_test_context* ctx) {
  async_loop_destroy(ctx->loop);
  zx_handle_close(ctx->client);
}

static bool float_near(float a, float b) { return fabsf(a - b) < 0.001; }

TEST(ThermalTests, GetTemperature) {
  struct thermal_test_context ctx;
  ASSERT_NO_FATAL_FAILURES(setup(&ctx));

  // 323.2 Kelvin is 50.05 degrees Celsius.
  acpi_tmp_call_out = 3232;

  float temp = 0.0f;
  zx_status_t status;
  EXPECT_OK(fuchsia_hardware_thermal_DeviceGetTemperatureCelsius(ctx.client, &status, &temp));
  EXPECT_OK(status);

  EXPECT_TRUE(float_near(temp, 50.05f));

  ASSERT_NO_FATAL_FAILURES(teardown(&ctx));
}

TEST(ThermalTests, GetTemperatureNegative) {
  struct thermal_test_context ctx;
  ASSERT_NO_FATAL_FAILURES(setup(&ctx));

  // 233.5 Kelvin is -39.65 degrees Celsius.
  acpi_tmp_call_out = 2335;

  float temp = 0.0f;
  zx_status_t status;
  EXPECT_OK(fuchsia_hardware_thermal_DeviceGetTemperatureCelsius(ctx.client, &status, &temp));
  EXPECT_OK(status);

  EXPECT_TRUE(float_near(temp, -39.65f));

  ASSERT_NO_FATAL_FAILURES(teardown(&ctx));
}

}  // namespace acpi_thermal
