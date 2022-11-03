// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_TESTING_FAKE_PAVER_H_
#define SRC_STORAGE_TESTING_FAKE_PAVER_H_

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.paver/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <threads.h>
#include <zircon/errors.h>
#include <zircon/time.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <zxtest/zxtest.h>

namespace paver_test {

enum class Command {
  kUnknown,
  kInitializeAbr,
  kQueryCurrentConfiguration,
  kQueryActiveConfiguration,
  kQueryConfigurationLastSetActive,
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

class FakePaver : public fidl::WireServer<fuchsia_paver::Paver>,
                  public fidl::WireServer<fuchsia_paver::BootManager>,
                  public fidl::WireServer<fuchsia_paver::DynamicDataSink> {
 public:
  zx_status_t Connect(async_dispatcher_t* dispatcher,
                      fidl::ServerEnd<fuchsia_paver::Paver> request);

  void FindDataSink(FindDataSinkRequestView request,
                    FindDataSinkCompleter::Sync& _completer) override;

  void UseBlockDevice(UseBlockDeviceRequestView request,
                      UseBlockDeviceCompleter::Sync& _completer) override;

  void FindBootManager(FindBootManagerRequestView request,
                       FindBootManagerCompleter::Sync& _completer) override;

  void QueryCurrentConfiguration(QueryCurrentConfigurationCompleter::Sync& completer) override;

  void FindSysconfig(FindSysconfigRequestView request,
                     FindSysconfigCompleter::Sync& _completer) override;

  void QueryActiveConfiguration(QueryActiveConfigurationCompleter::Sync& completer) override;

  void QueryConfigurationLastSetActive(

      QueryConfigurationLastSetActiveCompleter::Sync& completer) override;

  void QueryConfigurationStatus(QueryConfigurationStatusRequestView request,
                                QueryConfigurationStatusCompleter::Sync& completer) override;

  void SetConfigurationActive(SetConfigurationActiveRequestView request,
                              SetConfigurationActiveCompleter::Sync& completer) override;

  void SetConfigurationUnbootable(SetConfigurationUnbootableRequestView request,
                                  SetConfigurationUnbootableCompleter::Sync& completer) override;

  void SetConfigurationHealthy(SetConfigurationHealthyRequestView request,
                               SetConfigurationHealthyCompleter::Sync& completer) override;

  void Flush(
      fidl::WireServer<fuchsia_paver::DynamicDataSink>::FlushCompleter::Sync& completer) override;

  void Flush(
      fidl::WireServer<fuchsia_paver::BootManager>::FlushCompleter::Sync& completer) override;

  void ReadAsset(ReadAssetRequestView request, ReadAssetCompleter::Sync& completer) override;

  void WriteAsset(WriteAssetRequestView request, WriteAssetCompleter::Sync& completer) override;

  void WriteOpaqueVolume(WriteOpaqueVolumeRequestView request,
                         WriteOpaqueVolumeCompleter::Sync& completer) override;

  void WriteFirmware(WriteFirmwareRequestView request,
                     WriteFirmwareCompleter::Sync& completer) override;

  void ReadFirmware(ReadFirmwareRequestView request,
                    ReadFirmwareCompleter::Sync& completer) override;

  void WriteVolumes(WriteVolumesRequestView request,
                    WriteVolumesCompleter::Sync& completer) override;

  void WriteBootloader(WriteBootloaderRequestView request,
                       WriteBootloaderCompleter::Sync& completer) override;

  void WipeVolume(WipeVolumeCompleter::Sync& completer) override;

  void InitializePartitionTables(InitializePartitionTablesCompleter::Sync& completer) override;

  void WipePartitionTables(WipePartitionTablesCompleter::Sync& completer) override;

  void WaitForWritten(size_t size);

  const std::vector<Command> GetCommandTrace();

  std::string last_firmware_type() const;
  fuchsia_paver::wire::Configuration last_firmware_config() const;
  fuchsia_paver::wire::Configuration last_asset_config() const;
  fuchsia_paver::wire::Asset last_asset() const;
  const std::string& data_file_path() const;

  void set_expected_payload_size(size_t size) { expected_payload_size_ = size; }
  void set_supported_firmware_type(std::string type);
  void set_abr_supported(bool supported) { abr_supported_ = supported; }
  void set_wait_for_start_signal(bool wait) { wait_for_start_signal_ = wait; }
  void set_expected_device(std::string expected);

  const AbrData abr_data();

  std::atomic<async_dispatcher_t*>& dispatcher() { return dispatcher_; }

 private:
  std::atomic<bool> wait_for_start_signal_ = false;
  sync_completion_t start_signal_;
  sync_completion_t done_signal_;
  std::atomic<size_t> signal_size_;

  mutable fbl::Mutex lock_;

  std::string last_firmware_type_ TA_GUARDED(lock_);
  fuchsia_paver::wire::Asset last_asset_ TA_GUARDED(lock_);
  fuchsia_paver::wire::Configuration last_firmware_config_ TA_GUARDED(lock_);
  fuchsia_paver::wire::Configuration last_asset_config_ TA_GUARDED(lock_);
  std::string data_file_path_ TA_GUARDED(lock_);

  std::atomic<size_t> expected_payload_size_ = 0;
  std::string expected_block_device_ TA_GUARDED(lock_);
  std::string supported_firmware_type_ TA_GUARDED(lock_);
  std::atomic<bool> abr_supported_ = false;
  AbrData abr_data_ TA_GUARDED(lock_) = kInitAbrData;

  std::atomic<async_dispatcher_t*> dispatcher_ = nullptr;

  std::vector<Command> command_trace_ TA_GUARDED(lock_);
  void AppendCommand(Command cmd) TA_REQ(lock_) { command_trace_.push_back(cmd); }
};

}  // namespace paver_test

#endif  // SRC_STORAGE_TESTING_FAKE_PAVER_H_
