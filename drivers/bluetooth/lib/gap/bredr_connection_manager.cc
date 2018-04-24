// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bredr_connection_manager.h"

#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"
#include "garnet/drivers/bluetooth/lib/hci/sequential_command_runner.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"
#include "lib/fsl/tasks/message_loop.h"

namespace btlib {
namespace gap {

namespace {

void SetPageScanEnabled(bool enabled, fxl::RefPtr<hci::Transport> hci,
                        async_t* dispatcher, hci::StatusCallback cb) {
  FXL_DCHECK(cb);
  auto read_enable = hci::CommandPacket::New(hci::kReadScanEnable);
  auto finish_enable_cb = [enabled, dispatcher, hci, finish_cb = std::move(cb)](
                              auto, const hci::EventPacket& event) {
    auto status = event.ToStatus();
    if (!status) {
      FXL_LOG(WARNING)
          << "gap: BrEdrConnectionManager: Read Scan Enable failed: "
          << status.ToString();
      finish_cb(status);
      return;
    }
    auto params = event.return_params<hci::ReadScanEnableReturnParams>();
    uint8_t scan_type = params->scan_enable;
    if (enabled) {
      scan_type |= static_cast<uint8_t>(hci::ScanEnableBit::kPage);
    } else {
      scan_type &= ~static_cast<uint8_t>(hci::ScanEnableBit::kPage);
    }
    auto write_enable = hci::CommandPacket::New(
        hci::kWriteScanEnable, sizeof(hci::WriteScanEnableCommandParams));
    write_enable->mutable_view()
        ->mutable_payload<hci::WriteScanEnableCommandParams>()
        ->scan_enable = scan_type;
    hci->command_channel()->SendCommand(
        std::move(write_enable), dispatcher,
        [cb = std::move(finish_cb), enabled](
            auto, const hci::EventPacket& event) { cb(event.ToStatus()); });
  };
  hci->command_channel()->SendCommand(std::move(read_enable), dispatcher,
                                      finish_enable_cb);
}

}  // namespace

BrEdrConnectionManager::BrEdrConnectionManager(fxl::RefPtr<hci::Transport> hci,
                                               RemoteDeviceCache* device_cache,
                                               bool use_interlaced_scan)
    : hci_(hci),
      cache_(device_cache),
      page_scan_interval_(0),
      page_scan_window_(0),
      use_interlaced_scan_(use_interlaced_scan),
      dispatcher_(async_get_default()),
      weak_ptr_factory_(this) {
  FXL_DCHECK(hci_);
  FXL_DCHECK(cache_);
  FXL_DCHECK(dispatcher_);

  hci_cmd_runner_ =
      std::make_unique<hci::SequentialCommandRunner>(dispatcher_, hci_);

  auto self = weak_ptr_factory_.GetWeakPtr();

  conn_complete_handler_id_ = hci_->command_channel()->AddEventHandler(
      hci::kConnectionCompleteEventCode,
      [self](const auto& event) {
        if (self)
          self->OnConnectionComplete(event);
      },
      dispatcher_);
  FXL_DCHECK(conn_complete_handler_id_);
  conn_request_handler_id_ = hci_->command_channel()->AddEventHandler(
      hci::kConnectionRequestEventCode,
      [self](const auto& event) {
        if (self)
          self->OnConnectionRequest(event);
      },
      dispatcher_);
  FXL_DCHECK(conn_request_handler_id_);
};

BrEdrConnectionManager::~BrEdrConnectionManager() {
  SetPageScanEnabled(false, hci_, dispatcher_, [](const auto) {});
  hci_->command_channel()->RemoveEventHandler(conn_request_handler_id_);
  conn_request_handler_id_ = 0;
  hci_->command_channel()->RemoveEventHandler(conn_complete_handler_id_);
  conn_complete_handler_id_ = 0;
}

void BrEdrConnectionManager::SetConnectable(bool connectable,
                                            hci::StatusCallback status_cb) {
  auto self = weak_ptr_factory_.GetWeakPtr();
  if (!connectable) {
    SetPageScanEnabled(false, hci_, dispatcher_,
                       [self, cb = std::move(status_cb)](const auto& status) {
                         if (self) {
                           self->page_scan_interval_ = 0;
                           self->page_scan_window_ = 0;
                         } else if (status) {
                           cb(hci::Status(common::HostError::kFailed));
                           return;
                         }
                         cb(status);
                       });
    return;
  }

  WritePageScanSettings(
      hci::kPageScanR1Interval, hci::kPageScanR1Window, use_interlaced_scan_,
      [self, cb = std::move(status_cb)](const auto& status) {
        if (!status) {
          FXL_LOG(WARNING) << "gap: BrEdrConnectionManager: Writing Page Scan "
                              "Settings Failed: "
                           << status.ToString();
          cb(status);
          return;
        }
        if (!self) {
          cb(hci::Status(common::HostError::kFailed));
          return;
        }
        SetPageScanEnabled(true, self->hci_, self->dispatcher_, std::move(cb));
      });
}

void BrEdrConnectionManager::WritePageScanSettings(uint16_t interval,
                                                   uint16_t window,
                                                   bool interlaced,
                                                   hci::StatusCallback cb) {
  auto self = weak_ptr_factory_.GetWeakPtr();
  if (!hci_cmd_runner_->IsReady()) {
    // TODO(jamuraa): could run the three "settings" commands in parallel and
    // remove the sequence runner.
    cb(hci::Status(common::HostError::kInProgress));
    return;
  }

  auto write_activity =
      hci::CommandPacket::New(hci::kWritePageScanActivity,
                              sizeof(hci::WritePageScanActivityCommandParams));
  auto* activity_params =
      write_activity->mutable_view()
          ->mutable_payload<hci::WritePageScanActivityCommandParams>();
  activity_params->page_scan_interval = htole16(interval);
  activity_params->page_scan_window = htole16(window);

  hci_cmd_runner_->QueueCommand(
      std::move(write_activity),
      [self, cb, interval, window](const hci::EventPacket& event) {
        if (!self) {
          cb(hci::Status(common::HostError::kFailed));
          return;
        }
        self->page_scan_interval_ = interval;
        self->page_scan_window_ = window;
        FXL_VLOG(2)
            << "gap: BrEdrConnectionManager: page scan activity updated";
      });

  auto write_type = hci::CommandPacket::New(
      hci::kWritePageScanType, sizeof(hci::WritePageScanTypeCommandParams));
  auto* type_params =
      write_type->mutable_view()
          ->mutable_payload<hci::WritePageScanTypeCommandParams>();
  type_params->page_scan_type = (interlaced ? hci::PageScanType::kInterlacedScan
                                            : hci::PageScanType::kStandardScan);

  hci_cmd_runner_->QueueCommand(
      std::move(write_type),
      [self, cb, interlaced](const hci::EventPacket& event) {
        if (!self) {
          cb(hci::Status(common::HostError::kFailed));
          return;
        }
        self->page_scan_type_ = (interlaced ? hci::PageScanType::kInterlacedScan
                                            : hci::PageScanType::kStandardScan);
        FXL_VLOG(2) << "gap: BrEdrConnectionManager: page scan type updated";
      });

  hci_cmd_runner_->RunCommands(cb);
}

void BrEdrConnectionManager::OnConnectionRequest(
    const hci::EventPacket& event) {
  FXL_LOG(INFO) << "gap: BrEdrConnectionManager: ignoring connection request";
}

void BrEdrConnectionManager::OnConnectionComplete(
    const hci::EventPacket& event) {
  FXL_LOG(INFO) << "gap: BrEdrConnectionManager: ignoring connection complete";
}

}  // namespace gap
}  // namespace btlib
