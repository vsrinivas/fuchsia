// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/x86/x86.h"

#include <fuchsia/hardware/acpi/llcpp/fidl.h>
#include <lib/driver-unit-test/utils.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/status.h>

#include <acpica/acpi.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>
#include <fbl/vector.h>

#include "src/devices/board/drivers/x86/include/acpi.h"
#include "src/devices/board/drivers/x86/include/smbios.h"
#include "src/devices/board/drivers/x86/include/sysmem.h"
#include "src/devices/board/drivers/x86/x64-bind.h"

using ::llcpp::fuchsia::hardware::acpi::MAX_ACPI_TABLE_ENTRIES;
using ::llcpp::fuchsia::hardware::acpi::wire::TableInfo;

zx_handle_t root_resource_handle;

static zx_status_t sys_device_suspend(void* ctx, uint8_t requested_state, bool enable_wake,
                                      uint8_t suspend_reason, uint8_t* out_state) {
  return acpi_suspend(requested_state, enable_wake, suspend_reason, out_state);
}

namespace x86 {

X86::~X86() {
  if (acpica_initialized_) {
    AcpiTerminate();
  }
}

int X86::Thread() {
  zx_status_t status = SysmemInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: SysmemInit() failed: %d", __func__, status);
    return status;
  }

  status = publish_acpi_devices(parent(), sys_root_, zxdev());
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: publish_acpi_devices() failed: %d", __func__, status);
    return status;
  }

  status = GoldfishControlInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GoldfishControlInit() failed: %d", __func__, status);
    return status;
  }
  return ZX_OK;
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

zx_status_t X86::Create(void* ctx, zx_device_t* parent, std::unique_ptr<X86>* out) {
  pbus_protocol_t pbus;

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  root_resource_handle = get_root_resource();

  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &pbus);
  if (status != ZX_OK) {
    return status;
  }

  // TODO(fxbug.dev/34631): Remove this use of device_get_parent().  For now, suppress this
  // deprecation warning to not spam the build logs
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  zx_device_t* sys_root = device_get_parent(parent);
#pragma GCC diagnostic pop
  if (sys_root == NULL) {
    zxlogf(ERROR, "%s: failed to find parent node of platform (expected sys)", __func__);
    return ZX_ERR_INTERNAL;
  }

  fbl::AllocChecker ac;
  *out = fbl::make_unique_checked<X86>(&ac, parent, &pbus, sys_root);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  return ZX_OK;
}

zx_status_t X86::CreateAndBind(void* ctx, zx_device_t* parent) {
  std::unique_ptr<X86> board;

  zx_status_t status = Create(ctx, parent, &board);
  if (status != ZX_OK) {
    return status;
  }

  status = board->Bind();
  if (status == ZX_OK) {
    // DevMgr now owns this pointer, release it to avoid destroying the
    // object when device goes out of scope.
    __UNUSED auto* ptr = board.release();
  }
  return status;
}

template <size_t N>
static void SetField(const char* label, const std::string& value, char (&out)[N]) {
  if (value.empty()) {
    zxlogf(ERROR, "acpi: smbios %s could not be read", label);
  } else if (value.size() >= N) {
    zxlogf(INFO, "acpi: smbios %s too big for sysinfo: %s", label, value.data());
  } else {
    strlcpy(out, value.data(), N);
  }
}

zx_status_t X86::Bind() {
  // Do early init of ACPICA etc.
  zx_status_t status = EarlyInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to perform early initialization %d ", __func__, status);
    return status;
  }

  // publish the board as ACPI root under /dev/sys/platform. PCI will get created under /dev/sys
  // (to preserve compatibility).
  status = DdkAdd("acpi", DEVICE_ADD_NON_BINDABLE);

  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi: error %d in device_add(sys/platform/acpi)", status);
    return status;
  }

  pbus_board_info_t board_info{.board_name = "x64", .board_revision = 0};
  pbus_bootloader_info_t bootloader_info{.vendor = "<unknown>"};

  // Load SMBIOS information.
  SmbiosInfo smbios;
  status = smbios.Load();
  if (status == ZX_OK) {
    SetField("board name", smbios.board_name(), board_info.board_name);
    SetField("vendor", smbios.vendor(), bootloader_info.vendor);
  }

  // Inform the platform bus of our board info.
  status = pbus_.SetBoardInfo(&board_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "SetBoardInfo failed: %d", status);
  }

  // Inform the platform bus of our bootloader info.
  status = pbus_.SetBootloaderInfo(&bootloader_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "SetBootloaderInfo failed: %d", status);
  }

  // Set the "sys" suspend op in platform-bus.
  // The devmgr coordinator code that arranges ordering in which the suspend hooks
  // are called makes sure the suspend hook attached to sys/ is called dead last,
  // (coordinator.cpp:BuildSuspendList()). If move this suspend hook elsewhere,
  // we must make sure that the coordinator code arranges for this suspend op to be
  // called last.
  pbus_sys_suspend_t suspend = {sys_device_suspend, NULL};
  status = pbus_.RegisterSysSuspendCallback(&suspend);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Could not register suspend callback: %d", __func__, status);
  }

  // Start up our protocol helpers and platform devices.
  return Start();
}

bool X86::RunUnitTests(void* ctx, zx_device_t* parent, zx_handle_t channel) {
  return driver_unit_test::RunZxTests("X86Tests", parent, channel);
}

zx_status_t X86::DdkMessage(fidl_incoming_msg* message, fidl_txn* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::hardware::acpi::Acpi::Dispatch(this, message, &transaction);
  return transaction.Status();
}

zx_status_t X86::GetAcpiTableEntries(fbl::Vector<TableInfo>* entries) {
  ZX_DEBUG_ASSERT(acpica_initialized_);

  for (uint32_t i = 0; i < MAX_ACPI_TABLE_ENTRIES; i++) {
    // Fetch the next table entry.
    ACPI_TABLE_HEADER* table;
    ACPI_STATUS status = AcpiGetTableByIndex(i, &table);
    if (status == AE_BAD_PARAMETER) {
      // End of iteration.
      break;
    } else if (status != AE_OK) {
      // Unexpected error.
      return ZX_ERR_INTERNAL;
    }

    // Create an ACPI table header entry.
    TableInfo info;
    static_assert(sizeof(info.name) == sizeof(table->Signature));
    memcpy(info.name.data(), table->Signature, sizeof(table->Signature));
    info.size = table->Length;

    // Add it to the list.
    fbl::AllocChecker ac;
    entries->push_back(info, &ac);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
  }

  return ZX_OK;
}

void X86::ListTableEntries(ListTableEntriesCompleter::Sync& completer) {
  ZX_DEBUG_ASSERT(acpica_initialized_);

  // Fetch the entries.
  fbl::Vector<TableInfo> entries;
  zx_status_t status = GetAcpiTableEntries(&entries);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }

  // Send them back to the user.
  completer.ReplySuccess(
      fidl::VectorView<TableInfo>(fidl::unowned_ptr(entries.data()), entries.size()));
}

void X86::ReadNamedTable(fidl::Array<uint8_t, 4> name, uint32_t instance, ::zx::vmo result,
                         ReadNamedTableCompleter::Sync& completer) {
  // Fetch the requested table.
  ACPI_TABLE_HEADER* table;
  if (ACPI_STATUS status = AcpiGetTable(reinterpret_cast<char*>(name.data()), instance, &table);
      status != AE_OK) {
    completer.ReplyError(status == AE_NOT_FOUND ? ZX_ERR_NOT_FOUND : ZX_ERR_INTERNAL);
    return;
  }

  // Copy it into the VMO.
  if (zx_status_t status =
          result.write(reinterpret_cast<uint8_t*>(table), /*offset=*/0, table->Length);
      status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }

  completer.ReplySuccess(table->Length);
}

static zx_driver_ops_t x86_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = X86::CreateAndBind;
  ops.run_unit_tests = X86::RunUnitTests;
  return ops;
}();

}  // namespace x86

ZIRCON_DRIVER(acpi_bus, x86::x86_driver_ops, "zircon", "0.1");
