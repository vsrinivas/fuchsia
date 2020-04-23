// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dev-pwrsrc.h"

#include <dirent.h>
#include <zircon/syscalls/port.h>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <zxtest/zxtest.h>

#define SIGNAL_WAIT_TIMEOUT (5000u)

namespace acpi_pwrsrc {

acpi_pwrsrc_device_t* dev;

ACPI_STATUS AcpiFakeEvaluatePsrOnline(ACPI_HANDLE handle, char* key, ACPI_OBJECT_LIST* args,
                                      ACPI_BUFFER* buffer) {
  if (strcmp("_PSR", key) == 0) {  // device power source state
    ACPI_OBJECT* obj = static_cast<ACPI_OBJECT*>(buffer->Pointer);
    EXPECT_NOT_NULL(obj);
    obj->Integer.Value = POWER_STATE_ONLINE;
  } else {
    return AE_ERROR;
  }
  return AE_OK;
}

void create_pwrsrc_device(void) {
  dev = static_cast<acpi_pwrsrc_device_t*>(calloc(1, sizeof(acpi_pwrsrc_device_t)));
  EXPECT_NOT_NULL(dev, "Failed to allocate memory for pwrsrc device");

  dev->acpi_handle = NULL;
  mtx_init(&dev->lock, mtx_plain);

  zx_status_t status = zx_event_create(0, &dev->event);
  EXPECT_OK(status);

  dev->info.type = POWER_TYPE_AC;
  dev->info.state = 0;
  dev->acpi_eval = &AcpiFakeEvaluatePsrOnline;
}

void release_pwrsrc_device(void) {
  if (dev->event != ZX_HANDLE_INVALID) {
    zx_handle_close(dev->event);
  }
  free(dev);
}

void setup(void) { create_pwrsrc_device(); }

void teardown(void) { release_pwrsrc_device(); }

void verify_psr_state_signal(bool should_signal) {
  zx_handle_t port;
  zx_status_t status = zx_port_create(0, &port);
  EXPECT_OK(status);

  status = zx_object_wait_async(dev->event, port, 0, ZX_USER_SIGNAL_0, ZX_WAIT_ASYNC_ONCE);
  EXPECT_OK(status);

  call_PSR(dev);

  zx_port_packet_t pkt;
  status = zx_port_wait(port, zx_deadline_after(ZX_MSEC(SIGNAL_WAIT_TIMEOUT)), &pkt);

  if (should_signal) {
    ASSERT_OK(status);
    ASSERT_EQ(ZX_PKT_TYPE_SIGNAL_ONE, pkt.type);
  } else {
    ASSERT_EQ(ZX_ERR_TIMED_OUT, status);
  }
}

TEST(TestCase, TestSignalOnPsrStateChanged) {
  setup();
  // mock PSR eval will always return 'online'
  // so make sure we start offline
  dev->info.state &= ~POWER_STATE_ONLINE;
  verify_psr_state_signal(true);
  teardown();
}

TEST(TestCase, TestNoSignalOnPsrStateUnchanged) {
  setup();
  // mock PSR eval will always return 'online'
  // so here we set the same.
  dev->info.state |= POWER_STATE_ONLINE;
  verify_psr_state_signal(false);
  teardown();
}

}  // namespace acpi_pwrsrc

// required stubs for faking ddk
zx_driver_rec_t __zircon_driver_rec__ = {};

void driver_printf(uint32_t flags, const char* fmt, ...) {}

const char* device_get_name(zx_device_t* device) { return "fake-acpi-pwrsrc"; }

zx_status_t device_add_from_driver(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                                   zx_device_t** out) {
  return ZX_OK;
}
