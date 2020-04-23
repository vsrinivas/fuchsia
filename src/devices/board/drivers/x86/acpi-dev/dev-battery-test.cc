// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dev-battery.h"

#include <dirent.h>
#include <zircon/syscalls/port.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

#define SIGNAL_WAIT_TIMEOUT (5000u)

namespace acpi_battery {

acpi_battery_device_t* dev;

ACPI_STATUS AcpiFakeEvaluateObject(ACPI_HANDLE handle, char* key, ACPI_OBJECT_LIST* args,
                                   ACPI_BUFFER* buffer) {
  if (strcmp("_STA", key) == 0) {  // device status - e.g battery removed
    ACPI_OBJECT* obj = static_cast<ACPI_OBJECT*>(buffer->Pointer);
    EXPECT_NOT_NULL(obj);
    obj->Integer.Value = 0u;
  } else if (strcmp("_BST", key) == 0) {  // battery status info
    ACPI_OBJECT* pkg = static_cast<ACPI_OBJECT*>(buffer->Pointer);
    EXPECT_EQ(pkg->Package.Count, 4);
    ACPI_OBJECT* elem = pkg->Package.Elements;
    EXPECT_NOT_NULL(elem);
    elem[2].Integer.Value = 50;
  } else {
    return AE_ERROR;
  }
  return AE_OK;
}

void create_battery_device(void) {
  dev = static_cast<acpi_battery_device_t*>(calloc(1, sizeof(acpi_battery_device_t)));
  EXPECT_NOT_NULL(dev, "Failed to allocate memory for battery device");

  ACPI_HANDLE acpi_handle = NULL;
  dev->acpi_handle = acpi_handle;

  mtx_init(&dev->lock, mtx_plain);

  zx_status_t status = zx_event_create(0, &dev->event);
  EXPECT_OK(status);

  dev->bst_buffer.Length = ACPI_ALLOCATE_BUFFER;
  dev->bst_buffer.Pointer = NULL;

  dev->bif_buffer.Length = ACPI_ALLOCATE_BUFFER;
  dev->bif_buffer.Pointer = NULL;

  dev->power_info.type = POWER_TYPE_BATTERY;

  dev->acpi_eval = &AcpiFakeEvaluateObject;
}

void release_battery_device(void) {
  if (dev->bst_buffer.Length != ACPI_ALLOCATE_BUFFER) {
    ACPI_FREE(dev->bst_buffer.Pointer);
  }
  if (dev->bif_buffer.Length != ACPI_ALLOCATE_BUFFER) {
    ACPI_FREE(dev->bif_buffer.Pointer);
  }
  if (dev->event != ZX_HANDLE_INVALID) {
    zx_handle_close(dev->event);
  }
  free(dev);
}

void setup(void) { create_battery_device(); }

void teardown(void) { release_battery_device(); }

void verify_battery_change_signal(uint32_t level, uint32_t state) {
  setup();

  zx_handle_t port;
  zx_status_t status = zx_port_create(0, &port);
  EXPECT_OK(status);

  status = zx_object_wait_async(dev->event, port, 0, ZX_USER_SIGNAL_0, ZX_WAIT_ASYNC_ONCE);
  EXPECT_OK(status);

  // fake values to trigger recognition of charging state
  ACPI_OBJECT elements[4];
  elements[0].Type = ACPI_TYPE_INTEGER;
  elements[0].Integer.Value = 0;
  elements[1].Type = ACPI_TYPE_INTEGER;
  elements[1].Integer.Value = 1;
  elements[2].Type = ACPI_TYPE_INTEGER;
  elements[2].Integer.Value = level;
  elements[3].Type = ACPI_TYPE_INTEGER;
  elements[3].Integer.Value = 5;

  ACPI_OBJECT pkg;
  pkg.Package.Count = 4;
  pkg.Package.Elements = elements;
  pkg.Type = ACPI_TYPE_PACKAGE;

  void* buf = ACPI_ALLOCATE_ZEROED((ACPI_SIZE)(sizeof(pkg)));
  const auto cleanup = fbl::AutoCall([buf]() { ACPI_FREE(buf); });
  dev->bst_buffer.Pointer = &pkg;

  // test simulates charge to 50
  dev->battery_info.last_full_capacity = 100;
  dev->battery_info.remaining_capacity = level;

  dev->power_info.state = state;

  call_BST(dev);

  zx_port_packet_t pkt;
  status = zx_port_wait(port, zx_deadline_after(ZX_MSEC(SIGNAL_WAIT_TIMEOUT)), &pkt);
  ASSERT_OK(status);
  ASSERT_EQ(ZX_PKT_TYPE_SIGNAL_ONE, pkt.type);

  teardown();
}

TEST(TestCase, TestSignalOnBatteryChargeLevel) { verify_battery_change_signal(49, 0); }

TEST(TestCase, TestSignalOnBatteryDischargeLevel) { verify_battery_change_signal(51, 0); }

TEST(TestCase, TestSignalOnBatteryChargeState) {
  verify_battery_change_signal(50, POWER_STATE_CHARGING);
}

TEST(TestCase, TestSignalOnBatteryDischargeState) {
  verify_battery_change_signal(50, POWER_STATE_DISCHARGING);
}

TEST(TestCase, TestSignalOnBatteryDisconnect) {
  setup();

  zx_handle_t port;
  zx_status_t status = zx_port_create(0, &port);
  EXPECT_OK(status);

  status = zx_object_wait_async(dev->event, port, 0, ZX_USER_SIGNAL_0, ZX_WAIT_ASYNC_ONCE);
  EXPECT_OK(status);

  dev->power_info.state = POWER_STATE_ONLINE;
  call_STA(dev);

  zx_port_packet_t pkt;
  status = zx_port_wait(port, zx_deadline_after(ZX_MSEC(SIGNAL_WAIT_TIMEOUT)), &pkt);
  ASSERT_OK(status);
  ASSERT_EQ(ZX_PKT_TYPE_SIGNAL_ONE, pkt.type);

  teardown();
}

}  // namespace acpi_battery

// required stubs for faking ddk
zx_driver_rec_t __zircon_driver_rec__ = {};

void driver_printf(uint32_t flags, const char* fmt, ...) {}

bool driver_log_severity_enabled_internal(const zx_driver_t* drv, uint32_t flag) { return false; }

void driver_logf_internal(const zx_driver_t* drv, uint32_t flag, const char* msg, ...) {}

const char* device_get_name(zx_device_t* device) { return "fake-acpi-battery"; }

zx_status_t device_add_from_driver(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                                   zx_device_t** out) {
  return ZX_OK;
}
