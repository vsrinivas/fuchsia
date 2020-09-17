// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_BRINGUP_BIN_NETSVC_TEST_PAVER_TEST_COMMON_H_
#define SRC_BRINGUP_BIN_NETSVC_TEST_PAVER_TEST_COMMON_H_

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <threads.h>
#include <zircon/boot/netboot.h>
#include <zircon/errors.h>
#include <zircon/time.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

#include "src/bringup/bin/netsvc/paver.h"

enum class Command {
  kUnknown,
  kInitializeAbr,
  kQueryCurrentConfiguration,
  kQueryActiveConfiguration,
  kQueryConfigurationStatus,
  kSetConfigurationActive,
  kSetConfigurationUnbootable,
  kSetActiveConfigurationHealthy,
  kReadAsset,
  kWriteAsset,
  kWriteFirmware,
  kWriteVolumes,
  kWriteBootloader,
  kWriteDataFile,
  kWipeVolume,
  kInitPartitionTables,
  kWipePartitionTables,
  kDataSinkFlush,
  kBootManagerFlush,
};

struct AbrSlotData {
  bool unbootable;
  bool active;
};

struct AbrData {
  AbrSlotData slot_a;
  AbrSlotData slot_b;
};

constexpr AbrData kInitAbrData = {
    .slot_a =
        {
            .unbootable = false,
            .active = false,
        },
    .slot_b =
        {
            .unbootable = false,
            .active = false,
        },
};

class FakePaver : public ::llcpp::fuchsia::paver::Paver::Interface,
                  public ::llcpp::fuchsia::paver::BootManager::Interface,
                  public ::llcpp::fuchsia::paver::DynamicDataSink::Interface {
 public:
  zx_status_t Connect(async_dispatcher_t* dispatcher, zx::channel request) {
    dispatcher_ = dispatcher;
    return fidl::BindSingleInFlightOnly<::llcpp::fuchsia::paver::Paver::Interface>(
        dispatcher, std::move(request), this);
  }

  void FindDataSink(zx::channel data_sink, FindDataSinkCompleter::Sync _completer) override {
    fidl::BindSingleInFlightOnly<::llcpp::fuchsia::paver::DynamicDataSink::Interface>(
        dispatcher_, std::move(data_sink), this);
  }

  void UseBlockDevice(zx::channel block_device, zx::channel dynamic_data_sink,
                      UseBlockDeviceCompleter::Sync _completer) override {
    auto result =
        ::llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(zx::unowned(block_device));
    if (!result.ok() || result->result.is_err()) {
      return;
    }
    const auto& path = result->result.response().path;
    if (std::string(path.data(), path.size()) != expected_block_device_) {
      return;
    }
    fidl::BindSingleInFlightOnly<::llcpp::fuchsia::paver::DynamicDataSink::Interface>(
        dispatcher_, std::move(dynamic_data_sink), this);
  }

  void FindBootManager(zx::channel boot_manager,
                       FindBootManagerCompleter::Sync _completer) override {
    AppendCommand(Command::kInitializeAbr);
    if (abr_supported_) {
      fidl::BindSingleInFlightOnly<::llcpp::fuchsia::paver::BootManager::Interface>(
          dispatcher_, std::move(boot_manager), this);
    }
  }

  void QueryCurrentConfiguration(QueryCurrentConfigurationCompleter::Sync completer) override {
    AppendCommand(Command::kQueryCurrentConfiguration);
    completer.ReplySuccess(::llcpp::fuchsia::paver::Configuration::A);
  }

  void FindSysconfig(zx::channel sysconfig, FindSysconfigCompleter::Sync _completer) override {}

  void QueryActiveConfiguration(QueryActiveConfigurationCompleter::Sync completer) override {
    AppendCommand(Command::kQueryActiveConfiguration);
    completer.ReplySuccess(::llcpp::fuchsia::paver::Configuration::A);
  }

  void QueryConfigurationStatus(::llcpp::fuchsia::paver::Configuration configuration,
                                QueryConfigurationStatusCompleter::Sync completer) override {
    AppendCommand(Command::kQueryConfigurationStatus);
    completer.ReplySuccess(::llcpp::fuchsia::paver::ConfigurationStatus::HEALTHY);
  }

  void SetConfigurationActive(::llcpp::fuchsia::paver::Configuration configuration,
                              SetConfigurationActiveCompleter::Sync completer) override {
    AppendCommand(Command::kSetConfigurationActive);
    zx_status_t status;
    switch (configuration) {
      case ::llcpp::fuchsia::paver::Configuration::A:
        abr_data_.slot_a.active = true;
        abr_data_.slot_a.unbootable = false;
        status = ZX_OK;
        break;

      case ::llcpp::fuchsia::paver::Configuration::B:
        abr_data_.slot_b.active = true;
        abr_data_.slot_b.unbootable = false;
        status = ZX_OK;
        break;

      case ::llcpp::fuchsia::paver::Configuration::RECOVERY:
        status = ZX_ERR_INVALID_ARGS;
        break;
    }
    completer.Reply(status);
  }

  void SetConfigurationUnbootable(::llcpp::fuchsia::paver::Configuration configuration,
                                  SetConfigurationUnbootableCompleter::Sync completer) override {
    AppendCommand(Command::kSetConfigurationUnbootable);
    zx_status_t status;
    switch (configuration) {
      case ::llcpp::fuchsia::paver::Configuration::A:
        abr_data_.slot_a.unbootable = true;
        status = ZX_OK;
        break;

      case ::llcpp::fuchsia::paver::Configuration::B:
        abr_data_.slot_b.unbootable = true;
        status = ZX_OK;
        break;

      case ::llcpp::fuchsia::paver::Configuration::RECOVERY:
        status = ZX_ERR_INVALID_ARGS;
        break;
    }
    completer.Reply(status);
  }

  void SetActiveConfigurationHealthy(
      SetActiveConfigurationHealthyCompleter::Sync completer) override {
    AppendCommand(Command::kSetActiveConfigurationHealthy);
    completer.Reply(ZX_OK);
  }

  void Flush(::llcpp::fuchsia::paver::DynamicDataSink::Interface::FlushCompleter::Sync completer)
      override {
    AppendCommand(Command::kDataSinkFlush);
    completer.Reply(ZX_OK);
  }

  void Flush(
      ::llcpp::fuchsia::paver::BootManager::Interface::FlushCompleter::Sync completer) override {
    AppendCommand(Command::kBootManagerFlush);
    completer.Reply(ZX_OK);
  }

  void ReadAsset(::llcpp::fuchsia::paver::Configuration configuration,
                 ::llcpp::fuchsia::paver::Asset asset,
                 ReadAssetCompleter::Sync completer) override {
    AppendCommand(Command::kReadAsset);
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void WriteAsset(::llcpp::fuchsia::paver::Configuration configuration,
                  ::llcpp::fuchsia::paver::Asset asset, ::llcpp::fuchsia::mem::Buffer payload,
                  WriteAssetCompleter::Sync completer) override {
    AppendCommand(Command::kWriteAsset);
    auto status = payload.size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
    completer.Reply(status);
  }

  void WriteFirmware(::llcpp::fuchsia::paver::Configuration configuration, fidl::StringView type,
                     ::llcpp::fuchsia::mem::Buffer payload,
                     WriteFirmwareCompleter::Sync completer) override {
    using ::llcpp::fuchsia::paver::WriteFirmwareResult;
    AppendCommand(Command::kWriteFirmware);
    last_firmware_type_ = std::string(type.data(), type.size());

    // Reply varies depending on whether we support |type| or not.
    if (supported_firmware_type_ == std::string_view(type.data(), type.size())) {
      auto status = payload.size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
      completer.Reply(WriteFirmwareResult::WithStatus(fidl::unowned_ptr(&status)));
    } else {
      fidl::aligned<bool> unsupported = true;
      completer.Reply(WriteFirmwareResult::WithUnsupported(fidl::unowned_ptr(&unsupported)));
    }
  }

  void WriteVolumes(zx::channel payload_stream, WriteVolumesCompleter::Sync completer) override {
    AppendCommand(Command::kWriteVolumes);
    // Register VMO.
    zx::vmo vmo;
    auto status = zx::vmo::create(1024, 0, &vmo);
    if (status != ZX_OK) {
      completer.Reply(status);
      return;
    }
    ::llcpp::fuchsia::paver::PayloadStream::SyncClient stream(std::move(payload_stream));
    auto result = stream.RegisterVmo(std::move(vmo));
    status = result.ok() ? result.value().status : result.status();
    if (status != ZX_OK) {
      completer.Reply(status);
      return;
    }
    // Stream until EOF.
    status = [&]() {
      size_t data_transferred = 0;
      for (;;) {
        if (wait_for_start_signal_) {
          sync_completion_wait(&start_signal_, ZX_TIME_INFINITE);
          sync_completion_reset(&start_signal_);
        } else {
          signal_size_ = expected_payload_size_ + 1;
        }
        while (data_transferred < signal_size_) {
          auto result = stream.ReadData();
          if (!result.ok()) {
            return result.status();
          }
          const auto& response = result.value();
          switch (response.result.which()) {
            case ::llcpp::fuchsia::paver::ReadResult::Tag::kErr:
              return response.result.err();
            case ::llcpp::fuchsia::paver::ReadResult::Tag::kEof:
              return data_transferred == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
            case ::llcpp::fuchsia::paver::ReadResult::Tag::kInfo:
              data_transferred += response.result.info().size;
              continue;
            default:
              return ZX_ERR_INTERNAL;
          }
        }
        sync_completion_signal(&done_signal_);
      }
    }();

    sync_completion_signal(&done_signal_);

    completer.Reply(status);
  }

  void WriteBootloader(::llcpp::fuchsia::mem::Buffer payload,
                       WriteBootloaderCompleter::Sync completer) override {
    AppendCommand(Command::kWriteBootloader);
    auto status = payload.size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
    completer.Reply(status);
  }

  void WriteDataFile(fidl::StringView filename, ::llcpp::fuchsia::mem::Buffer payload,
                     WriteDataFileCompleter::Sync completer) override {
    AppendCommand(Command::kWriteDataFile);
    auto status = payload.size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
    completer.Reply(status);
  }

  void WipeVolume(WipeVolumeCompleter::Sync completer) override {
    AppendCommand(Command::kWipeVolume);
    completer.ReplySuccess({});
  }

  void InitializePartitionTables(InitializePartitionTablesCompleter::Sync completer) override {
    AppendCommand(Command::kInitPartitionTables);
    completer.Reply(ZX_OK);
  }

  void WipePartitionTables(WipePartitionTablesCompleter::Sync completer) override {
    AppendCommand(Command::kWipePartitionTables);
    completer.Reply(ZX_OK);
  }

  void WaitForWritten(size_t size) {
    signal_size_ = size;
    sync_completion_signal(&start_signal_);
    sync_completion_wait(&done_signal_, ZX_TIME_INFINITE);
    sync_completion_reset(&done_signal_);
  }

  const std::vector<Command> GetCommandTrace() { return command_trace_; }

  const std::string& last_firmware_type() const { return last_firmware_type_; }

  void set_expected_payload_size(size_t size) { expected_payload_size_ = size; }
  void set_supported_firmware_type(std::string type) { supported_firmware_type_ = type; }
  void set_abr_supported(bool supported) { abr_supported_ = supported; }
  void set_wait_for_start_signal(bool wait) { wait_for_start_signal_ = wait; }
  void set_expected_device(std::string expected) { expected_block_device_ = expected; }

  AbrData& abr_data() { return abr_data_; }

 private:
  bool wait_for_start_signal_ = false;
  sync_completion_t start_signal_;
  sync_completion_t done_signal_;
  std::atomic<size_t> signal_size_;

  std::string last_firmware_type_;

  size_t expected_payload_size_ = 0;
  std::string expected_block_device_;
  std::string supported_firmware_type_;
  bool abr_supported_ = false;
  AbrData abr_data_ = kInitAbrData;

  async_dispatcher_t* dispatcher_ = nullptr;

  std::vector<Command> command_trace_;
  void AppendCommand(Command cmd) { command_trace_.push_back(cmd); }
};

class FakeSvc {
 public:
  explicit FakeSvc(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher), vfs_(dispatcher) {
    auto root_dir = fbl::MakeRefCounted<fs::PseudoDir>();
    root_dir->AddEntry(::llcpp::fuchsia::paver::Paver::Name,
                       fbl::MakeRefCounted<fs::Service>([this](zx::channel request) {
                         return fake_paver_.Connect(dispatcher_, std::move(request));
                       }));

    zx::channel svc_remote;
    ASSERT_OK(zx::channel::create(0, &svc_local_, &svc_remote));

    vfs_.ServeDirectory(root_dir, std::move(svc_remote));
  }

  FakePaver& fake_paver() { return fake_paver_; }
  zx::channel& svc_chan() { return svc_local_; }

 private:
  async_dispatcher_t* dispatcher_;
  fs::SynchronousVfs vfs_;
  FakePaver fake_paver_;
  zx::channel svc_local_;
};

class FakeDev {
 public:
  FakeDev() {
    driver_integration_test::IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");
    args.path_prefix = "/pkg/";

    ASSERT_OK(driver_integration_test::IsolatedDevmgr::Create(&args, &devmgr_));
    fbl::unique_fd fd;
    ASSERT_OK(
        devmgr_integration_test::RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform", &fd));
  }

  driver_integration_test::IsolatedDevmgr devmgr_;
};

class PaverTest : public zxtest::Test {
 protected:
  PaverTest()
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        fake_svc_(loop_.dispatcher()),
        paver_(std::move(fake_svc_.svc_chan()), fake_dev_.devmgr_.devfs_root().duplicate()) {
    paver_.set_timeout(zx::msec(500));
    loop_.StartThread("paver-test-loop");
  }

  ~PaverTest() {
    // Need to make sure paver thread exits.
    Wait();
    if (ramdisk_ != nullptr) {
      ramdisk_destroy(ramdisk_);
      ramdisk_ = nullptr;
    }
    loop_.Shutdown();
  }

  void Wait() {
    while (paver_.InProgress())
      continue;
  }

  void SpawnBlockDevice() {
    fbl::unique_fd fd;
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(fake_dev_.devmgr_.devfs_root(),
                                                            "misc/ramctl", &fd));
    ASSERT_OK(
        ramdisk_create_at(fake_dev_.devmgr_.devfs_root().get(), ZX_PAGE_SIZE, 100, &ramdisk_));
    std::string expected = std::string("/dev/") + ramdisk_get_path(ramdisk_);
    fake_svc_.fake_paver().set_expected_device(expected);
  }

  async::Loop loop_;
  ramdisk_client_t* ramdisk_ = nullptr;
  FakeSvc fake_svc_;
  FakeDev fake_dev_;
  netsvc::Paver paver_;
};

#endif  // SRC_BRINGUP_BIN_NETSVC_TEST_PAVER_TEST_COMMON_H_
