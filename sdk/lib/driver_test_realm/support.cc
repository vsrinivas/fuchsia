// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// To get drivermanager to run in a test environment, we need to fake boot-arguments & root-job.

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.driver.test/cpp/wire.h>
#include <fidl/fuchsia.kernel/cpp/wire.h>
#include <fidl/fuchsia.power.manager/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async/bind.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/svc/dir.h>
#include <lib/svc/outgoing.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/global.h>
#include <lib/vfs/cpp/remote_dir.h>
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
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/pseudo_file.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "zircon/system/public/zircon/device/vfs.h"

namespace {

constexpr zx_signals_t kDriverTestRealmStartSignal = ZX_USER_SIGNAL_1;

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

class FakeBootItems final : public fidl::WireServer<fuchsia_boot::Items> {
  void Get(GetRequestView request, GetCompleter::Sync& completer) override {
    zx::vmo vmo;
    uint32_t length = 0;
    std::vector<board_test::DeviceEntry> entries = {};
    zx_status_t status = GetBootItem(entries, request->type, request->extra, &vmo, &length);
    if (status != ZX_OK) {
      FX_LOGF(ERROR, nullptr, "Failed to get boot items: %d", status);
    }
    completer.Reply(std::move(vmo), length);
  }

  void GetBootloaderFile(GetBootloaderFileRequestView request,
                         GetBootloaderFileCompleter::Sync& completer) override {
    completer.Reply(zx::vmo());
  }
};

class FakeDriverIndex final : public fidl::WireServer<fuchsia_driver_framework::DriverIndex> {
  void MatchDriver(MatchDriverRequestView request, MatchDriverCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_FOUND);
  }

  void WaitForBaseDrivers(WaitForBaseDriversRequestView request,
                          WaitForBaseDriversCompleter::Sync& completer) override {
    completer.Reply();
  }
  void MatchDriversV1(MatchDriversV1RequestView request,
                      MatchDriversV1Completer::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_FOUND);
  }
};

class FakeRootJob final : public fidl::WireServer<fuchsia_kernel::RootJob> {
  void Get(GetRequestView request, GetCompleter::Sync& completer) override {
    zx::job job;
    zx_status_t status = zx::job::default_job()->duplicate(ZX_RIGHT_SAME_RIGHTS, &job);
    if (status != ZX_OK) {
      FX_LOGF(ERROR, nullptr, "Failed to duplicate job: %d", status);
    }
    completer.Reply(std::move(job));
  }
};

class DriverTestRealm final : public fidl::WireServer<fuchsia_driver_test::Realm> {
 public:
  DriverTestRealm(svc::Outgoing* outgoing, async::Loop* loop) : outgoing_(outgoing), loop_(loop) {}

  static zx::status<std::unique_ptr<DriverTestRealm>> Create(svc::Outgoing* outgoing,
                                                             async::Loop* loop) {
    auto realm = std::make_unique<DriverTestRealm>(outgoing, loop);
    zx_status_t status = realm->Initialize();
    if (status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(std::move(realm));
  }

  void Start(StartRequestView request, StartCompleter::Sync& completer) override {
    if (is_started_) {
      completer.ReplyError(ZX_ERR_ALREADY_EXISTS);
      return;
    }

    std::map<std::string, std::string> boot_args;
    boot_args["devmgr.require-system"] = "true";
    boot_arguments_ = mock_boot_arguments::Server(std::move(boot_args));

    fidl::ClientEnd<fuchsia_io::Directory> boot_dir;
    if (request->args.has_boot()) {
      boot_dir = fidl::ClientEnd<fuchsia_io::Directory>(std::move(request->args.boot()));
    } else {
      auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
      if (endpoints.status_value() != ZX_OK) {
        completer.ReplyError(ZX_ERR_INTERNAL);
        return;
      }
      zx_status_t status =
          fdio_open("/pkg", ZX_FS_FLAG_DIRECTORY | ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_EXECUTABLE,
                    endpoints->server.TakeChannel().release());
      if (status != ZX_OK) {
        completer.ReplyError(ZX_ERR_INTERNAL);
        return;
      }
      boot_dir = std::move(endpoints->client);
    }
    outgoing_->root_dir()->AddEntry("boot",
                                    fbl::MakeRefCounted<fs::RemoteDir>(std::move(boot_dir)));

    start_event_.signal(0, kDriverTestRealmStartSignal);
    completer.ReplySuccess();
  }

 private:
  zx_status_t Initialize() {
    zx_status_t status = zx::event::create(0, &start_event_);
    if (status != ZX_OK) {
      return ZX_ERR_INTERNAL;
    }

    status = AddProtocol<fuchsia_driver_test::Realm>(this);
    if (status != ZX_OK) {
      return ZX_ERR_INTERNAL;
    }

    status = AddProtocolWithWait<fuchsia_boot::Arguments>(&boot_arguments_);
    if (status != ZX_OK) {
      return ZX_ERR_INTERNAL;
    }

    status = AddProtocolWithWait<fuchsia_boot::Items>(&boot_items_);
    if (status != ZX_OK) {
      return ZX_ERR_INTERNAL;
    }

    status = AddProtocolWithWait<fuchsia_kernel::RootJob>(&root_job_);
    if (status != ZX_OK) {
      return ZX_ERR_INTERNAL;
    }

    status = AddProtocolWithWait<fuchsia_power_manager::DriverManagerRegistration>(
        &fake_power_registration_);
    if (status != ZX_OK) {
      return ZX_ERR_INTERNAL;
    }

    status = InitializeDirectories();
    if (status != ZX_OK) {
      return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
  }

  zx_status_t InitializeDirectories() {
    auto system = fbl::MakeRefCounted<fs::PseudoDir>();
    system->AddEntry("drivers", fbl::MakeRefCounted<fs::PseudoDir>());
    outgoing_->root_dir()->AddEntry("system", std::move(system));

    auto pkgfs = fbl::MakeRefCounted<fs::PseudoDir>();
    // Add the necessary empty base driver manifest.
    // It's added to /pkgfs/packages/driver-manager-base-config/0/config/base-driver-manifest.json
    {
      auto packages = fbl::MakeRefCounted<fs::PseudoDir>();
      auto driver_manager_base_config = fbl::MakeRefCounted<fs::PseudoDir>();
      auto zero = fbl::MakeRefCounted<fs::PseudoDir>();
      auto config = fbl::MakeRefCounted<fs::PseudoDir>();
      auto base_driver_manifest = fbl::MakeRefCounted<fs::UnbufferedPseudoFile>(
          [](fbl::String* output) {
            // Return an empty JSON array.
            *output = fbl::String("[]");
            return ZX_OK;
          },
          [](std::string_view input) { return ZX_ERR_NOT_SUPPORTED; });

      config->AddEntry("base-driver-manifest.json", std::move(base_driver_manifest));
      zero->AddEntry("config", std::move(config));
      driver_manager_base_config->AddEntry("0", std::move(zero));
      packages->AddEntry("driver-manager-base-config", std::move(driver_manager_base_config));
      pkgfs->AddEntry("packages", std::move(packages));
    }
    outgoing_->root_dir()->AddEntry("pkgfs", std::move(pkgfs));
    return ZX_OK;
  }

  template <class Protocol>
  zx_status_t AddProtocolWithWait(fidl::WireServer<Protocol>* server) {
    auto service_callback = [this, server](fidl::ServerEnd<Protocol> request) {
      auto wait =
          std::make_shared<async::WaitOnce>(start_event_.get(), kDriverTestRealmStartSignal);
      auto wait_callback = [wait_object = wait, request = std::move(request), server](
                               async_dispatcher_t* dispatcher, async::WaitOnce* wait,
                               zx_status_t status, const zx_packet_signal_t* signal) mutable {
        if (status == ZX_OK) {
          fidl::BindServer(dispatcher, std::move(request), server);
        }
      };
      return wait->Begin(loop_->dispatcher(), std::move(wait_callback));
    };

    return outgoing_->svc_dir()->AddEntry(
        fidl::DiscoverableProtocolName<Protocol>,
        fbl::MakeRefCounted<fs::Service>(std::move(service_callback)));
  }

  template <class Protocol>
  zx_status_t AddProtocol(fidl::WireServer<Protocol>* server) {
    auto service_callback = [this, server](fidl::ServerEnd<Protocol> request) {
      fidl::BindServer(loop_->dispatcher(), std::move(request), server);
      return ZX_OK;
    };
    return outgoing_->svc_dir()->AddEntry(
        fidl::DiscoverableProtocolName<Protocol>,
        fbl::MakeRefCounted<fs::Service>(std::move(service_callback)));
  }

  svc::Outgoing* outgoing_;
  async::Loop* loop_;

  mock_boot_arguments::Server boot_arguments_;
  FakePowerRegistration fake_power_registration_;
  FakeBootItems boot_items_;
  FakeRootJob root_job_;

  zx::event start_event_;
  bool is_started_ = false;
};

}  // namespace

int main() {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  svc::Outgoing outgoing(loop.dispatcher());
  zx_status_t status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    return status;
  }

  auto realm = DriverTestRealm::Create(&outgoing, &loop);
  if (realm.status_value() != ZX_OK) {
    return realm.status_value();
  }

  loop.Run();
  return 0;
}
