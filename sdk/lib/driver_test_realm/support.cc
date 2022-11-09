// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// To get drivermanager to run in a test environment, we need to fake boot-arguments & root-job.

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.component.resolution/cpp/wire.h>
#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <fidl/fuchsia.diagnostics/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.driver.index/cpp/wire.h>
#include <fidl/fuchsia.driver.test/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.kernel/cpp/wire.h>
#include <fidl/fuchsia.pkg/cpp/wire.h>
#include <fidl/fuchsia.power.manager/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/component/cpp/incoming/service_client.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async/bind.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/cpp/wire/vector_view.h>
#include <lib/stdcompat/string_view.h>
#include <lib/svc/dir.h>
#include <lib/svc/outgoing.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/global.h>
#include <lib/vfs/cpp/remote_dir.h>
#include <lib/zx/job.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <zircon/boot/image.h>
#include <zircon/status.h>

#include <charconv>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

#include <ddk/metadata/test.h>
#include <fbl/string_printf.h>
#include <mock-boot-arguments/server.h>

#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/pseudo_file.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"

namespace {

constexpr zx_signals_t kDriverTestRealmStartSignal = ZX_USER_SIGNAL_1;

const char* LogLevelToString(fuchsia_diagnostics::wire::Severity severity) {
  switch (severity) {
    case fuchsia_diagnostics::wire::Severity::kTrace:
      return "TRACE";
    case fuchsia_diagnostics::wire::Severity::kDebug:
      return "DEBUG";
    case fuchsia_diagnostics::wire::Severity::kInfo:
      return "INFO";
    case fuchsia_diagnostics::wire::Severity::kWarn:
      return "WARN";
    case fuchsia_diagnostics::wire::Severity::kError:
      return "ERROR";
    case fuchsia_diagnostics::wire::Severity::kFatal:
      return "FATAL";
  }
}

// This board driver knows how to interpret the metadata for which devices to
// spawn.
const zbi_platform_id_t kPlatformId = []() {
  zbi_platform_id_t plat_id = {};
  plat_id.vid = PDEV_VID_TEST;
  plat_id.pid = PDEV_PID_PBUS_TEST;
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
                        std::string_view board_name, uint32_t extra, zx::vmo* out,
                        uint32_t* length) {
  zx::vmo vmo;
  switch (type) {
    case ZBI_TYPE_PLATFORM_ID: {
      zbi_platform_id_t platform_id = kPlatformId;
      if (!board_name.empty()) {
        strncpy(platform_id.board_name, board_name.data(), ZBI_BOARD_NAME_LEN - 1);
      }
      zx_status_t status = zx::vmo::create(sizeof(kPlatformId), 0, &vmo);
      if (status != ZX_OK) {
        return status;
      }
      status = vmo.write(&platform_id, 0, sizeof(kPlatformId));
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
 public:
  void Get(GetRequestView request, GetCompleter::Sync& completer) override {
    zx::vmo vmo;
    uint32_t length = 0;
    std::vector<board_test::DeviceEntry> entries = {};
    zx_status_t status =
        GetBootItem(entries, request->type, board_name_, request->extra, &vmo, &length);
    if (status != ZX_OK) {
      FX_SLOG(ERROR, "Failed to get boot items", KV("status", status));
    }
    completer.Reply(std::move(vmo), length);
  }

  void GetBootloaderFile(GetBootloaderFileRequestView request,
                         GetBootloaderFileCompleter::Sync& completer) override {
    completer.Reply(zx::vmo());
  }

  std::string board_name_;
};

class FakeDriverIndex final : public fidl::WireServer<fuchsia_driver_index::DriverIndex> {
  void MatchDriver(MatchDriverRequestView request, MatchDriverCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_FOUND);
  }

  void WaitForBaseDrivers(WaitForBaseDriversCompleter::Sync& completer) override {
    completer.Reply();
  }
  void MatchDriversV1(MatchDriversV1RequestView request,
                      MatchDriversV1Completer::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_FOUND);
  }
  void AddDeviceGroup(AddDeviceGroupRequestView request,
                      AddDeviceGroupCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_FOUND);
  }
};

class FakeRootJob final : public fidl::WireServer<fuchsia_kernel::RootJob> {
  void Get(GetCompleter::Sync& completer) override {
    zx::job job;
    zx_status_t status = zx::job::default_job()->duplicate(ZX_RIGHT_SAME_RIGHTS, &job);
    if (status != ZX_OK) {
      FX_SLOG(ERROR, "Failed to duplicate job", KV("status", status));
    }
    completer.Reply(std::move(job));
  }
};

class FakeBootResolver final : public fidl::WireServer<fuchsia_component_resolution::Resolver> {
 public:
  void SetPkgDir(fbl::RefPtr<fs::RemoteDir> pkg_dir) { pkg_dir_ = std::move(pkg_dir); }

 private:
  void Resolve(ResolveRequestView request, ResolveCompleter::Sync& completer) override {
    std::string_view kPrefix = "fuchsia-boot:///";
    std::string_view relative_path = request->component_url.get();
    if (!cpp20::starts_with(relative_path, kPrefix)) {
      completer.ReplyError(fuchsia_component_resolution::wire::ResolverError::kInvalidArgs);
      return;
    }
    relative_path.remove_prefix(kPrefix.size() + 1);

    auto file = fidl::CreateEndpoints<fuchsia_io::File>();
    if (file.is_error()) {
      completer.ReplyError(fuchsia_component_resolution::wire::ResolverError::kInternal);
      return;
    }
    zx_status_t status =
        fdio_open_at(pkg_dir_->client_end().channel()->get(), std::string(relative_path).data(),
                     static_cast<uint32_t>(fuchsia_io::wire::OpenFlags::kRightReadable),
                     file->server.channel().release());
    if (status != ZX_OK) {
      completer.ReplyError(fuchsia_component_resolution::wire::ResolverError::kInternal);
      return;
    }
    fidl::WireResult result =
        fidl::WireCall(file->client)->GetBackingMemory(fuchsia_io::wire::VmoFlags::kRead);
    if (!result.ok()) {
      completer.ReplyError(fuchsia_component_resolution::wire::ResolverError::kIo);
      return;
    }
    auto& response = result.value();
    if (response.is_error()) {
      completer.ReplyError(fuchsia_component_resolution::wire::ResolverError::kIo);
      return;
    }
    zx::vmo& vmo = response.value()->vmo;
    uint64_t size;
    status = vmo.get_prop_content_size(&size);
    if (status != ZX_OK) {
      completer.ReplyError(fuchsia_component_resolution::wire::ResolverError::kIo);
    }

    zx::result directory = component::Clone(pkg_dir_->client_end());
    if (directory.is_error()) {
      completer.ReplyError(fuchsia_component_resolution::wire::ResolverError::kInternal);
      return;
    }

    fidl::Arena arena;
    fuchsia_component_resolution::wire::Package package(arena);
    package.set_url(arena, fidl::StringView::FromExternal(kPrefix));
    package.set_directory(std::move(directory.value()));

    fuchsia_component_resolution::wire::Component component(arena);
    component.set_url(arena, request->component_url);
    component.set_decl(arena, fuchsia_mem::wire::Data::WithBuffer(arena, fuchsia_mem::wire::Buffer{
                                                                             .vmo = std::move(vmo),
                                                                             .size = size,
                                                                         }));
    component.set_package(arena, package);
    completer.ReplySuccess(component);
  }

  void ResolveWithContext(ResolveWithContextRequestView request,
                          ResolveWithContextCompleter::Sync& completer) override {
    FX_SLOG(ERROR, "FakeBootResolver does not currently support ResolveWithContext");
    completer.ReplyError(fuchsia_component_resolution::wire::ResolverError::kInvalidArgs);
  }

  fbl::RefPtr<fs::RemoteDir> pkg_dir_;
};

class FakePackageResolver final : public fidl::WireServer<fuchsia_pkg::PackageResolver> {
  void Resolve(ResolveRequestView request, ResolveCompleter::Sync& completer) override {
    auto status = fdio_open("/pkg",
                            static_cast<uint32_t>(fuchsia_io::wire::OpenFlags::kDirectory |
                                                  fuchsia_io::wire::OpenFlags::kRightReadable |
                                                  fuchsia_io::wire::OpenFlags::kRightExecutable),
                            request->dir.TakeChannel().release());
    if (status != ZX_OK) {
      completer.ReplyError(fuchsia_pkg::wire::ResolveError::kInternal);
      return;
    }

    auto resolution_context = fuchsia_pkg::wire::ResolutionContext({});
    completer.ReplySuccess(resolution_context);
  }

  void ResolveWithContext(ResolveWithContextRequestView request,
                          ResolveWithContextCompleter::Sync& completer) override {
    FX_SLOG(ERROR, "ResolveWithContext is not yet implemented in FakePackageResolver");
    completer.ReplyError(fuchsia_pkg::wire::ResolveError::kInternal);
  }

  void GetHash(GetHashRequestView request, GetHashCompleter::Sync& completer) override {
    constexpr size_t kHexadecimalBase = 16;
    constexpr size_t kNumBytesInMerkleRoot = 32;
    constexpr size_t kNumBytesInMerkleRootString = 64;

    std::ifstream meta_file("/pkg/meta");
    if (!meta_file.is_open()) {
      FX_SLOG(ERROR, "Failed to open \"/pkg/meta\"");
      completer.ReplyError(ZX_ERR_INTERNAL);
    }
    std::stringstream buffer;
    buffer << meta_file.rdbuf();
    auto meta_contents = buffer.str();
    if (meta_contents.length() != kNumBytesInMerkleRootString) {
      FX_SLOG(ERROR, "Mismatch in \"pkg_meta\" contents bytes",
              KV("expected", kNumBytesInMerkleRootString), KV("actual", meta_contents.length()));
      completer.ReplyError(ZX_ERR_INTERNAL);
    }

    fidl::Array<uint8_t, kNumBytesInMerkleRoot> merkle_root;
    for (unsigned int i = 0; i < kNumBytesInMerkleRoot; ++i) {
      auto byte_str = meta_contents.substr(i * 2, 2);
      uint8_t byte;
      auto result = std::from_chars(byte_str.data(), byte_str.data() + byte_str.size(), byte,
                                    kHexadecimalBase);
      if (result.ec == std::errc::invalid_argument) {
        FX_SLOG(ERROR, "Failed to convert contents \"/pkg/meta\" into a merkleroot");
        completer.ReplyError(ZX_ERR_INTERNAL);
      }
      merkle_root[i] = byte;
    }

    completer.ReplySuccess(fuchsia_pkg::wire::BlobId{merkle_root});
  }
};

class DriverTestRealm final : public fidl::WireServer<fuchsia_driver_test::Realm> {
 public:
  DriverTestRealm(svc::Outgoing* outgoing, async::Loop* loop) : outgoing_(outgoing), loop_(loop) {}

  static zx::result<std::unique_ptr<DriverTestRealm>> Create(svc::Outgoing* outgoing,
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

    if (request->args.has_board_name()) {
      boot_items_.board_name_ =
          std::string(request->args.board_name().data(), request->args.board_name().size());
    }

    auto boot_args = CreateBootArgs(request);
    for (std::pair<std::string, std::string> boot_arg : boot_args) {
      if (boot_arg.first.size() > fuchsia_boot::wire::kMaxArgsNameLength) {
        FX_SLOG(ERROR, "The length of the name of the boot argument is too long",
                KV("arg", boot_arg.first.data()),
                KV("maximum_length", fuchsia_boot::wire::kMaxArgsNameLength));
        completer.ReplyError(ZX_ERR_INVALID_ARGS);
        return;
      }

      if (boot_arg.second.size() > fuchsia_boot::wire::kMaxArgsValueLength) {
        FX_SLOG(ERROR, "The length of the value of the boot argument is too long",
                KV("arg", boot_arg.first.data()), KV("value", boot_arg.second.data()),
                KV("maximum_length", fuchsia_boot::wire::kMaxArgsValueLength));
        completer.ReplyError(ZX_ERR_INVALID_ARGS);
        return;
      }
    }
    boot_arguments_ = mock_boot_arguments::Server(std::move(boot_args));

    fidl::ClientEnd<fuchsia_io::Directory> boot_dir;
    if (request->args.has_boot()) {
      boot_dir = fidl::ClientEnd<fuchsia_io::Directory>(std::move(request->args.boot()));
    } else {
      auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
      if (endpoints.is_error()) {
        completer.ReplyError(ZX_ERR_INTERNAL);
        return;
      }
      zx_status_t status =
          fdio_open("/pkg",
                    static_cast<uint32_t>(fuchsia_io::wire::OpenFlags::kDirectory |
                                          fuchsia_io::wire::OpenFlags::kRightReadable |
                                          fuchsia_io::wire::OpenFlags::kRightExecutable),
                    endpoints->server.channel().release());
      if (status != ZX_OK) {
        completer.ReplyError(ZX_ERR_INTERNAL);
        return;
      }
      boot_dir = std::move(endpoints->client);
    }

    auto remote_dir = fbl::MakeRefCounted<fs::RemoteDir>(std::move(boot_dir));
    boot_resolver_.SetPkgDir(remote_dir);
    outgoing_->root_dir()->AddEntry("boot", remote_dir);

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

    status = AddProtocolWithWait<fuchsia_component_resolution::Resolver>(&boot_resolver_);
    if (status != ZX_OK) {
      return ZX_ERR_INTERNAL;
    }

    status = AddProtocolWithWait<fuchsia_power_manager::DriverManagerRegistration>(
        &fake_power_registration_);
    if (status != ZX_OK) {
      return ZX_ERR_INTERNAL;
    }

    status = AddProtocolWithWait<fuchsia_pkg::PackageResolver>(&package_resolver_);
    if (status != ZX_OK) {
      return ZX_ERR_INTERNAL;
    }

    status = InitializeDirectories();
    if (status != ZX_OK) {
      return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
  }

  static std::map<std::string, std::string> CreateBootArgs(StartRequestView& request) {
    std::map<std::string, std::string> boot_args;

    bool is_dfv2 = false;
    if (request->args.has_use_driver_framework_v2()) {
      is_dfv2 = request->args.use_driver_framework_v2();
    }

    boot_args["devmgr.enable-ephemeral"] = "true";
    boot_args["devmgr.require-system"] = "true";
    if (is_dfv2) {
      boot_args["driver_manager.use_driver_framework_v2"] = "true";
    }
    if (request->args.has_root_driver()) {
      boot_args["driver_manager.root-driver"] =
          std::string(request->args.root_driver().data(), request->args.root_driver().size());
    } else {
      if (is_dfv2) {
        boot_args["driver_manager.root-driver"] = "fuchsia-boot:///#meta/test-parent-sys.cm";
      } else {
        boot_args["driver_manager.root-driver"] = "fuchsia-boot:///#driver/test-parent-sys.so";
      }
    }

    if (request->args.has_driver_tests_enable_all() && request->args.driver_tests_enable_all()) {
      boot_args["driver.tests.enable"] = "true";
    }

    if (request->args.has_driver_tests_enable()) {
      for (auto& driver : request->args.driver_tests_enable()) {
        auto string = fbl::StringPrintf("driver.%s.tests.enable", driver.data());
        boot_args[string.data()] = "true";
      }
    }

    if (request->args.has_driver_tests_disable()) {
      for (auto& driver : request->args.driver_tests_disable()) {
        auto string = fbl::StringPrintf("driver.%s.tests.enable", driver.data());
        boot_args[string.data()] = "false";
      }
    }

    if (request->args.has_driver_log_level()) {
      for (auto& driver : request->args.driver_log_level()) {
        auto string = fbl::StringPrintf("driver.%s.log", driver.name.data());
        boot_args[string.data()] = LogLevelToString(driver.log_level);
      }
    }

    if (request->args.has_driver_disable()) {
      std::vector<std::string_view> drivers(request->args.driver_disable().count());
      for (auto& driver : request->args.driver_disable()) {
        drivers.emplace_back(driver.get());
        auto string = fbl::StringPrintf("driver.%s.disable", driver.data());
        boot_args[string.data()] = "true";
      }
      boot_args["devmgr.disabled-drivers"] = fxl::JoinStrings(drivers, ",");
    }

    if (request->args.has_driver_bind_eager() && request->args.driver_bind_eager().count() > 0) {
      std::vector<std::string_view> drivers(request->args.driver_bind_eager().count());
      for (auto& driver : request->args.driver_bind_eager()) {
        drivers.emplace_back(driver.data());
      }
      boot_args["devmgr.bind-eager"] = fxl::JoinStrings(drivers, ",");
    }

    return boot_args;
  }

  zx_status_t InitializeDirectories() {
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
  FakeBootResolver boot_resolver_;
  FakePackageResolver package_resolver_;

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
