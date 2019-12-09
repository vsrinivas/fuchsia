// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "x86.h"

#include <fuchsia/sysinfo/c/fidl.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/status.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>

#include "acpi.h"
#include "smbios.h"
#include "sysmem.h"

zx_handle_t root_resource_handle;

static zx_status_t sys_device_suspend(void* ctx, uint8_t requested_state, bool enable_wake,
                                      uint8_t suspend_reason, uint8_t* out_state) {
  return acpi_suspend(requested_state, enable_wake, suspend_reason, out_state);
}

namespace x86 {

int X86::Thread() {
  zx_status_t status = SysmemInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: SysmemInit() failed: %d\n", __func__, status);
    return status;
  }
  return publish_acpi_devices(parent(), sys_root_, zxdev());
}

zx_status_t X86::Start() {
  int rc = thrd_create_with_name(
      &thread_, [](void* arg) -> int { return reinterpret_cast<X86*>(arg)->Thread(); }, this,
      "x86_start_thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

void X86::DdkRelease() {
  int exit_code;
  thrd_join(thread_, &exit_code);
  delete this;
}

zx_status_t X86::Create(void* ctx, zx_device_t* parent) {
  pbus_protocol_t pbus;

  // Please do not use get_root_resource() in new code. See ZX-1467.
  root_resource_handle = get_root_resource();

  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &pbus);
  if (status != ZX_OK) {
    return status;
  }

  // TODO(ZX-4858): Remove this use of device_get_parent().  For now, suppress this
  // deprecation warning to not spam the build logs
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  zx_device_t* sys_root = device_get_parent(parent);
#pragma GCC diagnostic pop
  if (sys_root == NULL) {
    zxlogf(ERROR, "%s: failed to find parent node of platform (expected sys)\n", __func__);
    return ZX_ERR_INTERNAL;
  }

  fbl::AllocChecker ac;
  auto board = fbl::make_unique_checked<X86>(&ac, parent, &pbus, sys_root);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // Do ACPI init.
  status = board->EarlyAcpiInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to initialize ACPI %d \n", __func__, status);
    return status;
  }

  // publish the board as ACPI root under /dev/sys/platform. PCI will get created under /dev/sys
  // (to preserve compatibility).
  status = board->DdkAdd("acpi", DEVICE_ADD_NON_BINDABLE);

  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi: error %d in device_add(sys/platform/acpi)\n", status);
    return status;
  }

  char board_name[fuchsia_sysinfo_SYSINFO_BOARD_NAME_LEN + 1];
  size_t board_name_actual = 0;
  status = smbios_get_board_name(board_name, sizeof(board_name), &board_name_actual);
  if (status != ZX_OK) {
    if (status == ZX_ERR_BUFFER_TOO_SMALL) {
      zxlogf(INFO, "acpi: smbios board name too big for sysinfo\n");
    } else if (status != ZX_ERR_NOT_FOUND) {
      zxlogf(ERROR, "acpi: smbios board name could not be read: %s\n",
             zx_status_get_string(status));
    }
    strcpy(board_name, "pc");
    board_name_actual = strlen(board_name) + 1;
  }

  // Publish board name to sysinfo driver.
  status = board->DdkPublishMetadata("/dev/misc/sysinfo", DEVICE_METADATA_BOARD_NAME, board_name,
                                     board_name_actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "DdkPublishMetadata(board_name) failed: %d\n", status);
  }

  constexpr uint32_t dummy_board_rev = 42;
  status = board->DdkPublishMetadata("/dev/misc/sysinfo", DEVICE_METADATA_BOARD_REVISION,
                                     &dummy_board_rev, sizeof(dummy_board_rev));
  if (status != ZX_OK) {
    zxlogf(ERROR, "DdkPublishMetadata(board_revision) failed: %d\n", status);
  }

  // Inform platform bus of our board name.
  pbus_board_info_t board_info = {};
  strlcpy(board_info.board_name, board_name, sizeof(board_info.board_name));
  status = board->pbus_.SetBoardInfo(&board_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "SetBoardInfo failed: %d\n", status);
  }

  // Set the "sys" suspend op in platform-bus.
  // The devmgr coordinator code that arranges ordering in which the suspend hooks
  // are called makes sure the suspend hook attached to sys/ is called dead last,
  // (coordinator.cpp:BuildSuspendList()). If move this suspend hook elsewhere,
  // we must make sure that the coordinator code arranges for this suspend op to be
  // called last.
  pbus_sys_suspend_t suspend = {sys_device_suspend, NULL};
  status = board->pbus_.RegisterSysSuspendCallback(&suspend);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Could not register suspend callback: %d\n", __func__, status);
  }

  // Start up our protocol helpers and platform devices.
  status = board->Start();
  if (status == ZX_OK) {
    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = board.release();
  }

  return status;
}

static zx_driver_ops_t x86_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = X86::Create;
  return ops;
}();

}  // namespace x86

ZIRCON_DRIVER_BEGIN(acpi_bus, x86::x86_driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PBUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_INTEL),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_X86), ZIRCON_DRIVER_END(acpi_bus)
