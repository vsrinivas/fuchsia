// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include "paver.h"

#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <zircon/boot/netboot.h>

#include <algorithm>
#include <memory>

#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>
#include <zxtest/zxtest.h>
#include "lib/sync/completion.h"
#include "zircon/time.h"

#include "lib/async/dispatcher.h"

enum class Command {
  kUnknown,
  kInitializeAbr,
  kQueryActiveConfiguration,
  kQueryConfigurationStatus,
  kSetConfigurationActive,
  kSetConfigurationUnbootable,
  kSetActiveConfigurationHealthy,
  kReadAsset,
  kWriteAsset,
  kWriteVolumes,
  kWriteBootloader,
  kWriteDataFile,
  kWipeVolume,
};

class FakePaver : public ::llcpp::fuchsia::paver::Paver::Interface,
                  public ::llcpp::fuchsia::paver::BootManager::Interface,
                  public ::llcpp::fuchsia::paver::DataSink::Interface {
 public:
  zx_status_t Connect(async_dispatcher_t* dispatcher, zx::channel request) {
    dispatcher_ = dispatcher;
    return fidl::Bind<::llcpp::fuchsia::paver::Paver::Interface>(dispatcher, std::move(request),
                                                                 this);
  }

  void FindDataSink(zx::channel data_sink, FindDataSinkCompleter::Sync _completer) override {
    fidl::Bind<::llcpp::fuchsia::paver::DataSink::Interface>(dispatcher_, std::move(data_sink),
                                                             this);
  }

  void UseBlockDevice(zx::channel _block_device, zx::channel _dynamic_data_sink,
                      UseBlockDeviceCompleter::Sync _completer) override {}

  void FindBootManager(zx::channel boot_manager, bool initialize,
                       FindBootManagerCompleter::Sync _completer) override {
    last_command_ = Command::kInitializeAbr;
    if (abr_supported_) {
      if (initialize) {
        abr_initialized_ = true;
      }
      if (abr_initialized_) {
        fidl::Bind<::llcpp::fuchsia::paver::BootManager::Interface>(dispatcher_,
                                                                    std::move(boot_manager), this);
      }
    }
  }

  void QueryActiveConfiguration(QueryActiveConfigurationCompleter::Sync completer) override {
    last_command_ = Command::kQueryActiveConfiguration;
    completer.ReplySuccess(::llcpp::fuchsia::paver::Configuration::A);
  }

  void QueryConfigurationStatus(::llcpp::fuchsia::paver::Configuration configuration,
                                QueryConfigurationStatusCompleter::Sync completer) override {
    last_command_ = Command::kQueryConfigurationStatus;
    completer.ReplySuccess(::llcpp::fuchsia::paver::ConfigurationStatus::HEALTHY);
  }

  void SetConfigurationActive(::llcpp::fuchsia::paver::Configuration configuration,
                              SetConfigurationActiveCompleter::Sync completer) override {
    last_command_ = Command::kSetConfigurationActive;
    zx_status_t status;
    if (configuration == ::llcpp::fuchsia::paver::Configuration::A) {
      status = ZX_OK;
    } else {
      status = ZX_ERR_INVALID_ARGS;
    }
    completer.Reply(status);
  }

  void SetConfigurationUnbootable(::llcpp::fuchsia::paver::Configuration configuration,
                                  SetConfigurationUnbootableCompleter::Sync completer) override {
    last_command_ = Command::kSetConfigurationUnbootable;
    zx_status_t status;
    if (configuration == ::llcpp::fuchsia::paver::Configuration::RECOVERY) {
      status = ZX_ERR_INVALID_ARGS;
    } else {
      status = ZX_OK;
    }
    completer.Reply(status);
  }

  void SetActiveConfigurationHealthy(
      SetActiveConfigurationHealthyCompleter::Sync completer) override {
    last_command_ = Command::kSetActiveConfigurationHealthy;
    completer.Reply(ZX_OK);
  }

  void ReadAsset(::llcpp::fuchsia::paver::Configuration configuration,
                 ::llcpp::fuchsia::paver::Asset asset,
                 ReadAssetCompleter::Sync completer) override {
    last_command_ = Command::kReadAsset;
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void WriteAsset(::llcpp::fuchsia::paver::Configuration configuration,
                  ::llcpp::fuchsia::paver::Asset asset, ::llcpp::fuchsia::mem::Buffer payload,
                  WriteAssetCompleter::Sync completer) override {
    last_command_ = Command::kWriteAsset;
    auto status = payload.size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
    completer.Reply(status);
  }

  void WriteVolumes(zx::channel payload_stream, WriteVolumesCompleter::Sync completer) override {
    last_command_ = Command::kWriteVolumes;
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
    last_command_ = Command::kWriteBootloader;
    auto status = payload.size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
    completer.Reply(status);
  }

  void WriteDataFile(fidl::StringView filename, ::llcpp::fuchsia::mem::Buffer payload,
                     WriteDataFileCompleter::Sync completer) override {
    last_command_ = Command::kWriteDataFile;
    auto status = payload.size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
    completer.Reply(status);
  }

  void WipeVolume(WipeVolumeCompleter::Sync completer) override {
    last_command_ = Command::kWipeVolume;
    completer.ReplySuccess({});
  }

  void WaitForWritten(size_t size) {
    signal_size_ = size;
    sync_completion_signal(&start_signal_);
    sync_completion_wait(&done_signal_, ZX_TIME_INFINITE);
    sync_completion_reset(&done_signal_);
  }

  Command last_command() { return last_command_; }
  void set_expected_payload_size(size_t size) { expected_payload_size_ = size; }
  void set_abr_supported(bool supported) { abr_supported_ = supported; }
  void set_wait_for_start_signal(bool wait) { wait_for_start_signal_ = wait; }

 private:
  bool wait_for_start_signal_ = false;
  sync_completion_t start_signal_;
  sync_completion_t done_signal_;
  std::atomic<size_t> signal_size_;

  Command last_command_ = Command::kUnknown;
  size_t expected_payload_size_ = 0;
  bool abr_supported_ = false;
  bool abr_initialized_ = false;

  async_dispatcher_t* dispatcher_ = nullptr;
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
    loop_.Shutdown();
  }

  void Wait() {
    while (paver_.InProgress())
      continue;
  }

  async::Loop loop_;
  FakeSvc fake_svc_;
  FakeDev fake_dev_;
  netsvc::Paver paver_;
};
