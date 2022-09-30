// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/x86/x86.h"

#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/driver-unit-test/utils.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/status.h>

#include <acpica/acpi.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>
#include <fbl/vector.h>

#include "src/devices/board/drivers/x86/include/acpi.h"
#include "src/devices/board/drivers/x86/include/sysmem.h"
#include "src/devices/board/drivers/x86/x64-bind.h"
#include "src/devices/board/lib/acpi/manager-fuchsia.h"
#include "src/devices/board/lib/smbios/smbios.h"

using fuchsia_hardware_acpi::wire::kMaxAcpiTableEntries;
using fuchsia_hardware_acpi::wire::TableInfo;

zx_handle_t root_resource_handle;

namespace x86 {

void SysSuspender::Callback(CallbackRequestView request, fdf::Arena& arena,
                            CallbackCompleter::Sync& completer) {
  uint8_t out_state;
  zx_status_t status = acpi_suspend(request->requested_state, request->enable_wake,
                                    request->suspend_reason, &out_state);
  completer.buffer(arena).Reply(status, out_state);
}

namespace fpbus = fuchsia_hardware_platform_bus;

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

  status = publish_acpi_devices(acpi_manager_.get(), parent(), zxdev());
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
  auto endpoints = fdf::CreateEndpoints<fpbus::PlatformBus>();
  if (endpoints.is_error()) {
    return endpoints.error_value();
  }

  zx_status_t status = device_connect_runtime_protocol(
      parent, fpbus::Service::PlatformBus::ServiceName, fpbus::Service::PlatformBus::Name,
      endpoints->server.TakeHandle().release());
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to connect to platform bus: %s", zx_status_get_string(status));
    return status;
  }

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  root_resource_handle = get_root_resource();

  fbl::AllocChecker ac;
  *out = fbl::make_unique_checked<X86>(&ac, parent, std::move(endpoints->client),
                                       std::make_unique<acpi::AcpiImpl>());
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

static void SetField(const char* label, const std::string& value, std::optional<std::string>& out) {
  if (value.empty()) {
    zxlogf(ERROR, "acpi: smbios %s could not be read", label);
  } else if (value.size() >= fpbus::kMaxInfoStringLength) {
    zxlogf(INFO, "acpi: smbios %s too big for sysinfo: %s", label, value.data());
  } else {
    out = value;
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
    zxlogf(ERROR, "acpi: error %d in device_add(sys/platform/platform-passthrough/acpi)", status);
    return status;
  }

  fpbus::BoardInfo board_info;
  board_info.board_name() = "x64";
  board_info.board_revision() = 0;
  fpbus::BootloaderInfo bootloader_info;
  bootloader_info.vendor() = "<unknown>";

  // Load SMBIOS information.
  smbios::SmbiosInfo smbios;
  status = smbios.Load();
  if (status == ZX_OK) {
    SetField("board name", smbios.board_name(), board_info.board_name());
    SetField("vendor", smbios.vendor(), bootloader_info.vendor());
  }

  fdf::Arena arena('X86I');
  {
    // Inform the platform bus of our board info.
    fidl::Arena fidl_arena;
    auto result = pbus_.buffer(arena)->SetBoardInfo(fidl::ToWire(fidl_arena, board_info));
    if (!result.ok()) {
      zxlogf(ERROR, "SetBoardInfo request failed: %s", result.FormatDescription().data());
    } else if (result->is_error()) {
      zxlogf(ERROR, "SetBoardInfo failed: %s", zx_status_get_string(result->error_value()));
    }
  }

  {
    // Inform the platform bus of our bootloader info.
    fidl::Arena fidl_arena;
    auto result = pbus_.buffer(arena)->SetBootloaderInfo(fidl::ToWire(fidl_arena, bootloader_info));
    if (!result.ok()) {
      zxlogf(ERROR, "SetBootloaderInfo request failed: %s", result.FormatDescription().data());
    } else if (result->is_error()) {
      zxlogf(ERROR, "SetBootloaderInfo failed: %s", zx_status_get_string(result->error_value()));
    }
  }

  // Set the "sys" suspend op in platform-bus.
  // The devmgr coordinator code that arranges ordering in which the suspend hooks
  // are called makes sure the suspend hook attached to sys/ is called dead last,
  // (coordinator.cpp:BuildSuspendList()). If move this suspend hook elsewhere,
  // we must make sure that the coordinator code arranges for this suspend op to be
  // called last.
  auto endpoints = fdf::CreateEndpoints<fpbus::SysSuspend>();
  if (endpoints.is_error()) {
    zxlogf(ERROR, "%s: Could not create suspend callback endpoints: %s", __func__,
           endpoints.status_string());
  } else {
    fdf::BindServer<fdf::WireServer<fpbus::SysSuspend>>(fdf::Dispatcher::GetCurrent()->get(),
                                                        std::move(endpoints->server), &suspender_);
    auto result = pbus_.buffer(arena)->RegisterSysSuspendCallback(std::move(endpoints->client));
    if (!result.ok()) {
      zxlogf(ERROR, "RegisterSysSuspendCallback request failed: %s",
             result.FormatDescription().data());
    } else if (result->is_error()) {
      zxlogf(ERROR, "RegisterSysSuspendCallback failed: %s",
             zx_status_get_string(result->error_value()));
    }
  }
  // Create the ACPI manager.
  acpi_manager_ = std::make_unique<acpi::FuchsiaManager>(acpi_.get(), &iommu_manager_, zxdev());

  // Start up our protocol helpers and platform devices.
  return Start();
}

bool X86::RunUnitTests(void* ctx, zx_device_t* parent, zx_handle_t channel) {
  return driver_unit_test::RunZxTests("X86Tests", parent, channel);
}

zx_status_t X86::GetAcpiTableEntries(fbl::Vector<TableInfo>* entries) {
  ZX_DEBUG_ASSERT(acpica_initialized_);

  for (uint32_t i = 0; i < kMaxAcpiTableEntries; i++) {
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
  completer.ReplySuccess(fidl::VectorView<TableInfo>::FromExternal(entries.data(), entries.size()));
}

void X86::ReadNamedTable(ReadNamedTableRequestView request,
                         ReadNamedTableCompleter::Sync& completer) {
  // Fetch the requested table.
  ACPI_TABLE_HEADER* table;
  if (ACPI_STATUS status =
          AcpiGetTable(reinterpret_cast<char*>(request->name.data()), request->instance, &table);
      status != AE_OK) {
    completer.ReplyError(status == AE_NOT_FOUND ? ZX_ERR_NOT_FOUND : ZX_ERR_INTERNAL);
    return;
  }

  // Copy it into the VMO.
  if (zx_status_t status =
          request->result.write(reinterpret_cast<uint8_t*>(table), /*offset=*/0, table->Length);
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
