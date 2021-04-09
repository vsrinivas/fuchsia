// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_BRINGUP_BIN_NETSVC_TEST_PAVER_TEST_COMMON_H_
#define SRC_BRINGUP_BIN_NETSVC_TEST_PAVER_TEST_COMMON_H_

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/client_end.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/service/llcpp/service.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <threads.h>
#include <zircon/boot/netboot.h>
#include <zircon/errors.h>
#include <zircon/time.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

#include "src/bringup/bin/netsvc/paver.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

enum class Command {
  kUnknown,
  kInitializeAbr,
  kQueryCurrentConfiguration,
  kQueryActiveConfiguration,
  kQueryConfigurationStatus,
  kSetConfigurationActive,
  kSetConfigurationUnbootable,
  kSetConfigurationHealthy,
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

class FakePaver : public fidl::WireInterface<fuchsia_paver::Paver>,
                  public fidl::WireInterface<fuchsia_paver::BootManager>,
                  public fidl::WireInterface<fuchsia_paver::DynamicDataSink> {
 public:
  zx_status_t Connect(async_dispatcher_t* dispatcher,
                      fidl::ServerEnd<fuchsia_paver::Paver> request) {
    dispatcher_ = dispatcher;
    return fidl::BindSingleInFlightOnly<fidl::WireInterface<fuchsia_paver::Paver>>(
        dispatcher, std::move(request), this);
  }

  void FindDataSink(fidl::ServerEnd<fuchsia_paver::DataSink> data_sink,
                    FindDataSinkCompleter::Sync& _completer) override {
    fidl::BindSingleInFlightOnly<fidl::WireInterface<fuchsia_paver::DynamicDataSink>>(
        dispatcher_, fidl::ServerEnd<fuchsia_paver::DynamicDataSink>(data_sink.TakeChannel()),
        this);
  }

  void UseBlockDevice(fidl::ClientEnd<fuchsia_hardware_block::Block> block_device,
                      fidl::ServerEnd<fuchsia_paver::DynamicDataSink> dynamic_data_sink,
                      UseBlockDeviceCompleter::Sync& _completer) override {
    fidl::UnownedClientEnd<fuchsia_device::Controller> controller(block_device.borrow().channel());
    auto result = WireCall(controller).GetTopologicalPath();
    if (!result.ok() || result->result.is_err()) {
      return;
    }
    const auto& path = result->result.response().path;
    {
      fbl::AutoLock al(&lock_);
      if (std::string(path.data(), path.size()) != expected_block_device_) {
        return;
      }
    }
    fidl::BindSingleInFlightOnly<fidl::WireInterface<fuchsia_paver::DynamicDataSink>>(
        dispatcher_, std::move(dynamic_data_sink), this);
  }

  void FindBootManager(fidl::ServerEnd<fuchsia_paver::BootManager> boot_manager,
                       FindBootManagerCompleter::Sync& _completer) override {
    fbl::AutoLock al(&lock_);
    AppendCommand(Command::kInitializeAbr);
    if (abr_supported_) {
      fidl::BindSingleInFlightOnly<fidl::WireInterface<fuchsia_paver::BootManager>>(
          dispatcher_, std::move(boot_manager), this);
    }
  }

  void QueryCurrentConfiguration(QueryCurrentConfigurationCompleter::Sync& completer) override {
    fbl::AutoLock al(&lock_);
    AppendCommand(Command::kQueryCurrentConfiguration);
    completer.ReplySuccess(fuchsia_paver::wire::Configuration::A);
  }

  void FindSysconfig(fidl::ServerEnd<fuchsia_paver::Sysconfig> sysconfig,
                     FindSysconfigCompleter::Sync& _completer) override {}

  void QueryActiveConfiguration(QueryActiveConfigurationCompleter::Sync& completer) override {
    fbl::AutoLock al(&lock_);
    AppendCommand(Command::kQueryActiveConfiguration);
    completer.ReplySuccess(fuchsia_paver::wire::Configuration::A);
  }

  void QueryConfigurationStatus(fuchsia_paver::wire::Configuration configuration,
                                QueryConfigurationStatusCompleter::Sync& completer) override {
    fbl::AutoLock al(&lock_);
    AppendCommand(Command::kQueryConfigurationStatus);
    completer.ReplySuccess(fuchsia_paver::wire::ConfigurationStatus::HEALTHY);
  }

  void SetConfigurationActive(fuchsia_paver::wire::Configuration configuration,
                              SetConfigurationActiveCompleter::Sync& completer) override {
    fbl::AutoLock al(&lock_);
    AppendCommand(Command::kSetConfigurationActive);
    zx_status_t status;
    switch (configuration) {
      case fuchsia_paver::wire::Configuration::A:
        abr_data_.slot_a.active = true;
        abr_data_.slot_a.unbootable = false;
        status = ZX_OK;
        break;

      case fuchsia_paver::wire::Configuration::B:
        abr_data_.slot_b.active = true;
        abr_data_.slot_b.unbootable = false;
        status = ZX_OK;
        break;

      case fuchsia_paver::wire::Configuration::RECOVERY:
        status = ZX_ERR_INVALID_ARGS;
        break;
    }
    completer.Reply(status);
  }

  void SetConfigurationUnbootable(fuchsia_paver::wire::Configuration configuration,
                                  SetConfigurationUnbootableCompleter::Sync& completer) override {
    fbl::AutoLock al(&lock_);
    AppendCommand(Command::kSetConfigurationUnbootable);
    zx_status_t status;
    switch (configuration) {
      case fuchsia_paver::wire::Configuration::A:
        abr_data_.slot_a.unbootable = true;
        status = ZX_OK;
        break;

      case fuchsia_paver::wire::Configuration::B:
        abr_data_.slot_b.unbootable = true;
        status = ZX_OK;
        break;

      case fuchsia_paver::wire::Configuration::RECOVERY:
        status = ZX_ERR_INVALID_ARGS;
        break;
    }
    completer.Reply(status);
  }

  void SetConfigurationHealthy(fuchsia_paver::wire::Configuration configuration,
                               SetConfigurationHealthyCompleter::Sync& completer) override {
    fbl::AutoLock al(&lock_);
    AppendCommand(Command::kSetConfigurationHealthy);
    completer.Reply(ZX_OK);
  }

  void Flush(fidl::WireInterface<fuchsia_paver::DynamicDataSink>::FlushCompleter::Sync& completer)
      override {
    fbl::AutoLock al(&lock_);
    AppendCommand(Command::kDataSinkFlush);
    completer.Reply(ZX_OK);
  }

  void Flush(
      fidl::WireInterface<fuchsia_paver::BootManager>::FlushCompleter::Sync& completer) override {
    fbl::AutoLock al(&lock_);
    AppendCommand(Command::kBootManagerFlush);
    completer.Reply(ZX_OK);
  }

  void ReadAsset(fuchsia_paver::wire::Configuration configuration, fuchsia_paver::wire::Asset asset,
                 ReadAssetCompleter::Sync& completer) override {
    fbl::AutoLock al(&lock_);
    AppendCommand(Command::kReadAsset);
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void WriteAsset(fuchsia_paver::wire::Configuration configuration,
                  fuchsia_paver::wire::Asset asset, fuchsia_mem::wire::Buffer payload,
                  WriteAssetCompleter::Sync& completer) override {
    fbl::AutoLock al(&lock_);
    AppendCommand(Command::kWriteAsset);
    auto status = payload.size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
    completer.Reply(status);
  }

  void WriteFirmware(fuchsia_paver::wire::Configuration configuration, fidl::StringView type,
                     fuchsia_mem::wire::Buffer payload,
                     WriteFirmwareCompleter::Sync& completer) override {
    using fuchsia_paver::wire::WriteFirmwareResult;
    fbl::AutoLock al(&lock_);
    AppendCommand(Command::kWriteFirmware);
    last_firmware_type_ = std::string(type.data(), type.size());

    // Reply varies depending on whether we support |type| or not.
    if (supported_firmware_type_ == std::string_view(type.data(), type.size())) {
      auto status = payload.size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
      completer.Reply(
          WriteFirmwareResult::WithStatus(fidl::ObjectView<zx_status_t>::FromExternal(&status)));
    } else {
      bool unsupported = true;
      completer.Reply(
          WriteFirmwareResult::WithUnsupported(fidl::ObjectView<bool>::FromExternal(&unsupported)));
    }
  }

  void WriteVolumes(fidl::ClientEnd<fuchsia_paver::PayloadStream> payload_stream,
                    WriteVolumesCompleter::Sync& completer) override {
    {
      fbl::AutoLock al(&lock_);
      AppendCommand(Command::kWriteVolumes);
    }
    // Register VMO.
    zx::vmo vmo;
    auto status = zx::vmo::create(1024, 0, &vmo);
    if (status != ZX_OK) {
      completer.Reply(status);
      return;
    }
    fidl::WireSyncClient<fuchsia_paver::PayloadStream> stream(std::move(payload_stream));
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
        {
          fbl::AutoLock al(&lock_);
          if (wait_for_start_signal_) {
            al.release();
            sync_completion_wait(&start_signal_, ZX_TIME_INFINITE);
            sync_completion_reset(&start_signal_);
          } else {
            signal_size_ = expected_payload_size_ + 1;
          }
        }
        while (data_transferred < signal_size_) {
          auto result = stream.ReadData();
          if (!result.ok()) {
            return result.status();
          }
          const auto& response = result.value();
          switch (response.result.which()) {
            case fuchsia_paver::wire::ReadResult::Tag::kErr:
              return response.result.err();
            case fuchsia_paver::wire::ReadResult::Tag::kEof:
              return data_transferred == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
            case fuchsia_paver::wire::ReadResult::Tag::kInfo:
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

  void WriteBootloader(fuchsia_mem::wire::Buffer payload,
                       WriteBootloaderCompleter::Sync& completer) override {
    fbl::AutoLock al(&lock_);
    AppendCommand(Command::kWriteBootloader);
    auto status = payload.size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
    completer.Reply(status);
  }

  void WriteDataFile(fidl::StringView filename, fuchsia_mem::wire::Buffer payload,
                     WriteDataFileCompleter::Sync& completer) override {
    fbl::AutoLock al(&lock_);
    AppendCommand(Command::kWriteDataFile);
    auto status = payload.size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
    completer.Reply(status);
  }

  void WipeVolume(WipeVolumeCompleter::Sync& completer) override {
    fbl::AutoLock al(&lock_);
    AppendCommand(Command::kWipeVolume);
    completer.ReplySuccess({});
  }

  void InitializePartitionTables(InitializePartitionTablesCompleter::Sync& completer) override {
    fbl::AutoLock al(&lock_);
    AppendCommand(Command::kInitPartitionTables);
    completer.Reply(ZX_OK);
  }

  void WipePartitionTables(WipePartitionTablesCompleter::Sync& completer) override {
    fbl::AutoLock al(&lock_);
    AppendCommand(Command::kWipePartitionTables);
    completer.Reply(ZX_OK);
  }

  void WaitForWritten(size_t size) {
    signal_size_ = size;
    sync_completion_signal(&start_signal_);
    sync_completion_wait(&done_signal_, ZX_TIME_INFINITE);
    sync_completion_reset(&done_signal_);
  }

  const std::vector<Command> GetCommandTrace() {
    fbl::AutoLock al(&lock_);
    return command_trace_;
  }

  std::string last_firmware_type() const {
    fbl::AutoLock al(&lock_);
    return last_firmware_type_;
  }

  void set_expected_payload_size(size_t size) { expected_payload_size_ = size; }
  void set_supported_firmware_type(std::string type) {
    fbl::AutoLock al(&lock_);
    supported_firmware_type_ = type;
  }
  void set_abr_supported(bool supported) { abr_supported_ = supported; }
  void set_wait_for_start_signal(bool wait) { wait_for_start_signal_ = wait; }
  void set_expected_device(std::string expected) {
    fbl::AutoLock al(&lock_);
    expected_block_device_ = expected;
  }

  const AbrData abr_data() {
    fbl::AutoLock al(&lock_);
    return abr_data_;
  }

 private:
  std::atomic<bool> wait_for_start_signal_ = false;
  sync_completion_t start_signal_;
  sync_completion_t done_signal_;
  std::atomic<size_t> signal_size_;

  mutable fbl::Mutex lock_;

  std::string last_firmware_type_ TA_GUARDED(lock_);

  std::atomic<size_t> expected_payload_size_ = 0;
  std::string expected_block_device_ TA_GUARDED(lock_);
  std::string supported_firmware_type_ TA_GUARDED(lock_);
  std::atomic<bool> abr_supported_ = false;
  AbrData abr_data_ TA_GUARDED(lock_) = kInitAbrData;

  std::atomic<async_dispatcher_t*> dispatcher_ = nullptr;

  std::vector<Command> command_trace_ TA_GUARDED(lock_);
  void AppendCommand(Command cmd) TA_REQ(lock_) { command_trace_.push_back(cmd); }
};

class FakeSvc {
 public:
  explicit FakeSvc(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher), vfs_(dispatcher) {
    auto root_dir = fbl::MakeRefCounted<fs::PseudoDir>();
    root_dir->AddEntry(
        fuchsia_paver::Paver::Name,
        fbl::MakeRefCounted<fs::Service>([this](fidl::ServerEnd<fuchsia_paver::Paver> request) {
          return fake_paver_.Connect(dispatcher_, std::move(request));
        }));

    auto server_end = fidl::CreateEndpoints(&svc_local_);
    ASSERT_OK(server_end.status_value());
    vfs_.ServeDirectory(root_dir, std::move(*server_end));
  }

  FakePaver& fake_paver() { return fake_paver_; }
  fidl::ClientEnd<fuchsia_io::Directory> TakeDirectory() { return std::move(svc_local_); }

 private:
  async_dispatcher_t* dispatcher_;
  fs::SynchronousVfs vfs_;
  FakePaver fake_paver_;
  fidl::ClientEnd<fuchsia_io::Directory> svc_local_;
};

class FakeDev {
 public:
  FakeDev() {
    driver_integration_test::IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");

    ASSERT_OK(driver_integration_test::IsolatedDevmgr::Create(&args, &devmgr_));
    fbl::unique_fd fd;
    ASSERT_OK(
        devmgr_integration_test::RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform", &fd));
  }

  fidl::ClientEnd<fuchsia_io::Directory> TakeDirectory() {
    auto caller = fdio_cpp::FdioCaller(devmgr_.devfs_root().duplicate());
    auto directory = caller.take_directory();
    EXPECT_OK(directory.status_value());  // Have to use EXPECT in functions that return a value.
    return std::move(*directory);
  }

  driver_integration_test::IsolatedDevmgr devmgr_;
};

class PaverTest : public zxtest::Test {
 protected:
  PaverTest()
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        fake_svc_(loop_.dispatcher()),
        paver_(fake_svc_.TakeDirectory(), fake_dev_.TakeDirectory()) {
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
    ASSERT_OK(ramdisk_create_at(fake_dev_.devmgr_.devfs_root().get(), zx_system_get_page_size(),
                                100, &ramdisk_));
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
