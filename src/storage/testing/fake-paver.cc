// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-paver.h"

namespace paver_test {

zx_status_t FakePaver::Connect(async_dispatcher_t* dispatcher,
                               fidl::ServerEnd<fuchsia_paver::Paver> request) {
  dispatcher_ = dispatcher;
  return fidl::BindSingleInFlightOnly<fidl::WireServer<fuchsia_paver::Paver>>(
      dispatcher, std::move(request), this);
}

void FakePaver::FindDataSink(FindDataSinkRequestView request,
                             FindDataSinkCompleter::Sync& _completer) {
  fidl::BindSingleInFlightOnly<fidl::WireServer<fuchsia_paver::DynamicDataSink>>(
      dispatcher_,
      fidl::ServerEnd<fuchsia_paver::DynamicDataSink>(request->data_sink.TakeChannel()), this);
}

void FakePaver::UseBlockDevice(UseBlockDeviceRequestView request,
                               UseBlockDeviceCompleter::Sync& _completer) {
  auto result = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_device::Controller>(
                                   request->block_device.borrow().channel()))
                    ->GetTopologicalPath();
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
  fidl::BindSingleInFlightOnly<fidl::WireServer<fuchsia_paver::DynamicDataSink>>(
      dispatcher_, std::move(request->data_sink), this);
}

void FakePaver::FindBootManager(FindBootManagerRequestView request,
                                FindBootManagerCompleter::Sync& _completer) {
  fbl::AutoLock al(&lock_);
  AppendCommand(Command::kInitializeAbr);
  if (abr_supported_) {
    fidl::BindSingleInFlightOnly<fidl::WireServer<fuchsia_paver::BootManager>>(
        dispatcher_, std::move(request->boot_manager), this);
  }
}

void FakePaver::QueryCurrentConfiguration(QueryCurrentConfigurationRequestView request,
                                          QueryCurrentConfigurationCompleter::Sync& completer) {
  fbl::AutoLock al(&lock_);
  AppendCommand(Command::kQueryCurrentConfiguration);
  completer.ReplySuccess(fuchsia_paver::wire::Configuration::kA);
}

void FakePaver::FindSysconfig(FindSysconfigRequestView request,
                              FindSysconfigCompleter::Sync& _completer) {}

void FakePaver::QueryActiveConfiguration(QueryActiveConfigurationRequestView request,
                                         QueryActiveConfigurationCompleter::Sync& completer) {
  fbl::AutoLock al(&lock_);
  AppendCommand(Command::kQueryActiveConfiguration);
  completer.ReplySuccess(fuchsia_paver::wire::Configuration::kA);
}

void FakePaver::QueryConfigurationLastSetActive(
    QueryConfigurationLastSetActiveRequestView request,
    QueryConfigurationLastSetActiveCompleter::Sync& completer) {
  fbl::AutoLock al(&lock_);
  AppendCommand(Command::kQueryConfigurationLastSetActive);
  completer.ReplySuccess(fuchsia_paver::wire::Configuration::kA);
}

void FakePaver::QueryConfigurationStatus(QueryConfigurationStatusRequestView request,
                                         QueryConfigurationStatusCompleter::Sync& completer) {
  fbl::AutoLock al(&lock_);
  AppendCommand(Command::kQueryConfigurationStatus);
  completer.ReplySuccess(fuchsia_paver::wire::ConfigurationStatus::kHealthy);
}

void FakePaver::SetConfigurationActive(SetConfigurationActiveRequestView request,
                                       SetConfigurationActiveCompleter::Sync& completer) {
  fbl::AutoLock al(&lock_);
  AppendCommand(Command::kSetConfigurationActive);
  zx_status_t status;
  switch (request->configuration) {
    case fuchsia_paver::wire::Configuration::kA:
      abr_data_.slot_a.active = true;
      abr_data_.slot_a.unbootable = false;
      status = ZX_OK;
      break;

    case fuchsia_paver::wire::Configuration::kB:
      abr_data_.slot_b.active = true;
      abr_data_.slot_b.unbootable = false;
      status = ZX_OK;
      break;

    case fuchsia_paver::wire::Configuration::kRecovery:
      status = ZX_ERR_INVALID_ARGS;
      break;
  }
  completer.Reply(status);
}

void FakePaver::SetConfigurationUnbootable(SetConfigurationUnbootableRequestView request,
                                           SetConfigurationUnbootableCompleter::Sync& completer) {
  fbl::AutoLock al(&lock_);
  AppendCommand(Command::kSetConfigurationUnbootable);
  zx_status_t status;
  switch (request->configuration) {
    case fuchsia_paver::wire::Configuration::kA:
      abr_data_.slot_a.unbootable = true;
      status = ZX_OK;
      break;

    case fuchsia_paver::wire::Configuration::kB:
      abr_data_.slot_b.unbootable = true;
      status = ZX_OK;
      break;

    case fuchsia_paver::wire::Configuration::kRecovery:
      status = ZX_ERR_INVALID_ARGS;
      break;
  }
  completer.Reply(status);
}

void FakePaver::SetConfigurationHealthy(SetConfigurationHealthyRequestView request,
                                        SetConfigurationHealthyCompleter::Sync& completer) {
  fbl::AutoLock al(&lock_);
  AppendCommand(Command::kSetConfigurationHealthy);
  completer.Reply(ZX_OK);
}

void FakePaver::Flush(
    fidl::WireServer<fuchsia_paver::DynamicDataSink>::FlushRequestView request,
    fidl::WireServer<fuchsia_paver::DynamicDataSink>::FlushCompleter::Sync& completer) {
  fbl::AutoLock al(&lock_);
  AppendCommand(Command::kDataSinkFlush);
  completer.Reply(ZX_OK);
}

void FakePaver::Flush(
    fidl::WireServer<fuchsia_paver::BootManager>::FlushRequestView request,
    fidl::WireServer<fuchsia_paver::BootManager>::FlushCompleter::Sync& completer) {
  fbl::AutoLock al(&lock_);
  AppendCommand(Command::kBootManagerFlush);
  completer.Reply(ZX_OK);
}

void FakePaver::ReadAsset(ReadAssetRequestView request, ReadAssetCompleter::Sync& completer) {
  fbl::AutoLock al(&lock_);
  AppendCommand(Command::kReadAsset);
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void FakePaver::WriteAsset(WriteAssetRequestView request, WriteAssetCompleter::Sync& completer) {
  fbl::AutoLock al(&lock_);
  AppendCommand(Command::kWriteAsset);
  auto status = request->payload.size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
  last_asset_ = request->asset;
  last_asset_config_ = request->configuration;
  completer.Reply(status);
}

void FakePaver::WriteFirmware(WriteFirmwareRequestView request,
                              WriteFirmwareCompleter::Sync& completer) {
  using fuchsia_paver::wire::WriteFirmwareResult;
  fbl::AutoLock al(&lock_);
  AppendCommand(Command::kWriteFirmware);
  last_firmware_type_ = std::string(request->type.data(), request->type.size());
  last_firmware_config_ = request->configuration;

  // Reply varies depending on whether we support |type| or not.
  if (supported_firmware_type_ == std::string_view(request->type.data(), request->type.size())) {
    auto status = request->payload.size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
    completer.Reply(WriteFirmwareResult::WithStatus(status));
  } else {
    completer.Reply(WriteFirmwareResult::WithUnsupported(true));
  }
}

void FakePaver::WriteVolumes(WriteVolumesRequestView request,
                             WriteVolumesCompleter::Sync& completer) {
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
  fidl::WireSyncClient stream = fidl::BindSyncClient(std::move(request->payload));
  auto result = stream->RegisterVmo(std::move(vmo));
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
        auto result = stream->ReadData();
        if (!result.ok()) {
          return result.status();
        }
        const auto& response = result.value();
        switch (response.result.Which()) {
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

void FakePaver::WriteBootloader(WriteBootloaderRequestView request,
                                WriteBootloaderCompleter::Sync& completer) {
  fbl::AutoLock al(&lock_);
  AppendCommand(Command::kWriteBootloader);
  auto status = request->payload.size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
  completer.Reply(status);
}

void FakePaver::WriteDataFile(WriteDataFileRequestView request,
                              WriteDataFileCompleter::Sync& completer) {
  fbl::AutoLock al(&lock_);
  AppendCommand(Command::kWriteDataFile);
  auto status = request->payload.size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
  completer.Reply(status);
}

void FakePaver::WipeVolume(WipeVolumeRequestView request, WipeVolumeCompleter::Sync& completer) {
  fbl::AutoLock al(&lock_);
  AppendCommand(Command::kWipeVolume);
  completer.ReplySuccess({});
}

void FakePaver::InitializePartitionTables(InitializePartitionTablesRequestView request,
                                          InitializePartitionTablesCompleter::Sync& completer) {
  fbl::AutoLock al(&lock_);
  AppendCommand(Command::kInitPartitionTables);
  completer.Reply(ZX_OK);
}

void FakePaver::WipePartitionTables(WipePartitionTablesRequestView request,
                                    WipePartitionTablesCompleter::Sync& completer) {
  fbl::AutoLock al(&lock_);
  AppendCommand(Command::kWipePartitionTables);
  completer.Reply(ZX_OK);
}

void FakePaver::WaitForWritten(size_t size) {
  signal_size_ = size;
  sync_completion_signal(&start_signal_);
  sync_completion_wait(&done_signal_, ZX_TIME_INFINITE);
  sync_completion_reset(&done_signal_);
}

const std::vector<Command> FakePaver::GetCommandTrace() {
  fbl::AutoLock al(&lock_);
  return command_trace_;
}

std::string FakePaver::last_firmware_type() const {
  fbl::AutoLock al(&lock_);
  return last_firmware_type_;
}

fuchsia_paver::wire::Configuration FakePaver::last_firmware_config() const {
  fbl::AutoLock al(&lock_);
  return last_firmware_config_;
}

fuchsia_paver::wire::Configuration FakePaver::last_asset_config() const {
  fbl::AutoLock al(&lock_);
  return last_asset_config_;
}

fuchsia_paver::wire::Asset FakePaver::last_asset() const {
  fbl::AutoLock al(&lock_);
  return last_asset_;
}

void FakePaver::set_supported_firmware_type(std::string type) {
  fbl::AutoLock al(&lock_);
  supported_firmware_type_ = type;
}

void FakePaver::set_expected_device(std::string expected) {
  fbl::AutoLock al(&lock_);
  expected_block_device_ = expected;
}

const AbrData FakePaver::abr_data() {
  fbl::AutoLock al(&lock_);
  return abr_data_;
}

}  // namespace paver_test
