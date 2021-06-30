// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// To get drivermanager to run in a test environment, we need to fake boot-arguments & root-job.

#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/boot/llcpp/fidl.h>
#include <fuchsia/device/manager/llcpp/fidl.h>
#include <fuchsia/driver/framework/llcpp/fidl.h>
#include <fuchsia/kernel/c/fidl.h>
#include <fuchsia/kernel/llcpp/fidl.h>
#include <fuchsia/power/manager/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fidl-async/bind.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/job.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <zircon/boot/image.h>
#include <zircon/status.h>

#include <memory>
#include <vector>

#include <ddk/metadata/test.h>
#include <mock-boot-arguments/server.h>

#include "lib/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"

namespace {

// This board driver knows how to interpret the metadata for which devices to
// spawn.
const zbi_platform_id_t kPlatformId = []() {
  zbi_platform_id_t plat_id = {};
  plat_id.vid = PDEV_VID_TEST;
  plat_id.pid = PDEV_PID_INTEGRATION_TEST;
  strcpy(plat_id.board_name, "driver-integration-test");
  return plat_id;
}();

#define BOARD_REVISION_TEST 42

const zbi_board_info_t kBoardInfo = []() {
  zbi_board_info_t board_info = {};
  board_info.revision = BOARD_REVISION_TEST;
  return board_info;
}();

// This function is responsible for serializing driver data. It must be kept
// updated with the function that deserialized the data. This function
// is TestBoard::FetchAndDeserialize.
zx_status_t GetBootItem(const std::vector<board_test::DeviceEntry>& entries, uint32_t type,
                        uint32_t extra, zx::vmo* out, uint32_t* length) {
  zx::vmo vmo;
  switch (type) {
    case ZBI_TYPE_PLATFORM_ID: {
      zx_status_t status = zx::vmo::create(sizeof(kPlatformId), 0, &vmo);
      if (status != ZX_OK) {
        return status;
      }
      status = vmo.write(&kPlatformId, 0, sizeof(kPlatformId));
      if (status != ZX_OK) {
        return status;
      }
      *length = sizeof(kPlatformId);
      break;
    }
    case ZBI_TYPE_DRV_BOARD_INFO: {
      zx_status_t status = zx::vmo::create(sizeof(kBoardInfo), 0, &vmo);
      if (status != ZX_OK) {
        return status;
      }
      status = vmo.write(&kBoardInfo, 0, sizeof(kBoardInfo));
      if (status != ZX_OK) {
        return status;
      }
      *length = sizeof(kBoardInfo);
      break;
    }
    case ZBI_TYPE_DRV_BOARD_PRIVATE: {
      size_t list_size = sizeof(board_test::DeviceList);
      size_t entry_size = entries.size() * sizeof(board_test::DeviceEntry);

      size_t metadata_size = 0;
      for (const board_test::DeviceEntry& entry : entries) {
        metadata_size += entry.metadata_size;
      }

      zx_status_t status = zx::vmo::create(list_size + entry_size + metadata_size, 0, &vmo);
      if (status != ZX_OK) {
        return status;
      }

      // Write DeviceList to vmo.
      board_test::DeviceList list{.count = entries.size()};
      status = vmo.write(&list, 0, sizeof(list));
      if (status != ZX_OK) {
        return status;
      }

      // Write DeviceEntries to vmo.
      status = vmo.write(entries.data(), list_size, entry_size);
      if (status != ZX_OK) {
        return status;
      }

      // Write Metadata to vmo.
      size_t write_offset = list_size + entry_size;
      for (const board_test::DeviceEntry& entry : entries) {
        status = vmo.write(entry.metadata, write_offset, entry.metadata_size);
        if (status != ZX_OK) {
          return status;
        }
        write_offset += entry.metadata_size;
      }

      *length = static_cast<uint32_t>(list_size + entry_size + metadata_size);
      break;
    }
    default:
      break;
  }
  *out = std::move(vmo);
  return ZX_OK;
}

class FakePowerRegistration
    : public fidl::WireServer<fuchsia_power_manager::DriverManagerRegistration> {
 public:
  void Register(RegisterRequestView request, RegisterCompleter::Sync& completer) override {
    // Store these so the other side doesn't see the channels close.
    transition_ = std::move(request->system_state_transition);
    dir_ = std::move(request->dir);
    completer.ReplySuccess();
  }

 private:
  fidl::ClientEnd<fuchsia_device_manager::SystemStateTransition> transition_;
  fidl::ClientEnd<fuchsia_io::Directory> dir_;
};

zx_status_t ItemsGet(void* ctx, uint32_t type, uint32_t extra, fidl_txn_t* txn) {
  zx::vmo vmo;
  uint32_t length = 0;
  std::vector<board_test::DeviceEntry> entries = {};
  zx_status_t status = GetBootItem(entries, type, extra, &vmo, &length);
  if (status != ZX_OK) {
    return status;
  }
  return fuchsia_boot_ItemsGet_reply(txn, vmo.release(), length);
}

constexpr fuchsia_boot_Items_ops kItemsOps = {
    .Get = ItemsGet,
};

class FakeDriverIndex final : public fidl::WireServer<fuchsia_driver_framework::DriverIndex> {
  void MatchDriver(MatchDriverRequestView request, MatchDriverCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_FOUND);
  }

  void WaitForBaseDrivers(WaitForBaseDriversRequestView request,
                          WaitForBaseDriversCompleter::Sync& completer) override {
    completer.Reply();
  }
};

}  // namespace

static zx_status_t RootJobGet(void* ctx, fidl_txn_t* txn) {
  zx_handle_t out;
  zx_status_t status = zx_handle_duplicate(zx_job_default(), ZX_RIGHT_SAME_RIGHTS, &out);
  if (status != ZX_OK) {
    return status;
  }
  return fuchsia_kernel_RootJobGet_reply(txn, out);
}

constexpr fuchsia_kernel_RootJob_ops kRootJobOps = {
    .Get = RootJobGet,
};

int main(void) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  mock_boot_arguments::Server boot_arguments;
  context->outgoing()->AddPublicService(
      std::make_unique<vfs::Service>(
          [&boot_arguments](zx::channel request, async_dispatcher_t* dispatcher) {
            fidl::BindSingleInFlightOnly(dispatcher, std::move(request), &boot_arguments);
          }),
      fidl::DiscoverableProtocolName<fuchsia_boot::Arguments>);
  FakePowerRegistration fake_power_registration;
  context->outgoing()->AddPublicService(
      std::make_unique<vfs::Service>(
          [&fake_power_registration](zx::channel request, async_dispatcher_t* dispatcher) {
            fidl::BindSingleInFlightOnly(dispatcher, std::move(request), &fake_power_registration);
          }),
      fidl::DiscoverableProtocolName<fuchsia_power_manager::DriverManagerRegistration>);
  context->outgoing()->AddPublicService(
      std::make_unique<vfs::Service>([](zx::channel request, async_dispatcher_t* dispatcher) {
        auto boot_items_dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_Items_dispatch);
        fidl_bind(dispatcher, request.release(), boot_items_dispatch, nullptr, &kItemsOps);
      }),
      fidl::DiscoverableProtocolName<fuchsia_boot::Items>);

  context->outgoing()->AddPublicService(
      std::make_unique<vfs::Service>([](zx::channel request, async_dispatcher_t* dispatcher) {
        auto root_job_dispatch =
            reinterpret_cast<fidl_dispatch_t*>(fuchsia_kernel_RootJob_dispatch);
        fidl_bind(dispatcher, request.release(), root_job_dispatch, nullptr, &kRootJobOps);
      }),
      fidl::DiscoverableProtocolName<fuchsia_kernel::RootJob>);

  FakeDriverIndex driver_index;
  context->outgoing()->AddPublicService(
      std::make_unique<vfs::Service>([&driver_index](zx::channel request,
                                                     async_dispatcher_t* dispatcher) {
        fidl::BindServer(dispatcher,
                         fidl::ServerEnd<fuchsia_driver_framework::DriverIndex>(std::move(request)),
                         &driver_index);
      }),
      fidl::DiscoverableProtocolName<fuchsia_driver_framework::DriverIndex>);

  context->outgoing()->root_dir()->AddEntry("system", std::make_unique<vfs::PseudoDir>());
  context->outgoing()->root_dir()->AddEntry("pkgfs", std::make_unique<vfs::PseudoDir>());

  loop.Run();
  return 0;
}
