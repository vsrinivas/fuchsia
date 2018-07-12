// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bredr_connection_manager.h"

#include "garnet/drivers/bluetooth/lib/gap/remote_device_cache.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"
#include "garnet/drivers/bluetooth/lib/hci/sequential_command_runner.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"

namespace btlib {
namespace gap {

namespace {

void SetPageScanEnabled(bool enabled, fxl::RefPtr<hci::Transport> hci,
                        async_dispatcher_t* dispatcher, hci::StatusCallback cb) {
  FXL_DCHECK(cb);
  auto read_enable = hci::CommandPacket::New(hci::kReadScanEnable);
  auto finish_enable_cb = [enabled, dispatcher, hci, finish_cb = std::move(cb)](
                              auto, const hci::EventPacket& event) mutable {
    if (BTEV_TEST_WARN(event, "gap (BR/EDR): Read Scan Enable failed")) {
      finish_cb(event.ToStatus());
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
                                      std::move(finish_enable_cb));
}

}  // namespace

BrEdrConnectionManager::BrEdrConnectionManager(fxl::RefPtr<hci::Transport> hci,
                                               RemoteDeviceCache* device_cache,
                                               bool use_interlaced_scan)
    : hci_(hci),
      cache_(device_cache),
      interrogator_(cache_, hci_, async_get_default_dispatcher()),
      page_scan_interval_(0),
      page_scan_window_(0),
      use_interlaced_scan_(use_interlaced_scan),
      dispatcher_(async_get_default_dispatcher()),
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
  disconn_cmpl_handler_id_ = hci->command_channel()->AddEventHandler(
      hci::kDisconnectionCompleteEventCode,
      [self](const auto& event) {
        if (self)
          self->OnDisconnectionComplete(event);
      },
      dispatcher_);

  FXL_DCHECK(conn_request_handler_id_);
};

BrEdrConnectionManager::~BrEdrConnectionManager() {
  // Disconnect any connections that we're holding.
  connections_.clear();
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
      [self, cb = std::move(status_cb)](const auto& status) mutable {
        if (!status) {
          FXL_LOG(WARNING) << "gap (BR/EDR): Write Page Scan Settings failed: "
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
      [self, interval, window](const hci::EventPacket& event) {
        if (BTEV_TEST_WARN(event,
                           "gap (BR/EDR): write page scan activity failed")) {
          return;
        }
        if (!self) {
          return;
        }
        self->page_scan_interval_ = interval;
        self->page_scan_window_ = window;
        FXL_VLOG(2) << "gap (BR/EDR): page scan activity updated";
      });

  auto write_type = hci::CommandPacket::New(
      hci::kWritePageScanType, sizeof(hci::WritePageScanTypeCommandParams));
  auto* type_params =
      write_type->mutable_view()
          ->mutable_payload<hci::WritePageScanTypeCommandParams>();
  type_params->page_scan_type = (interlaced ? hci::PageScanType::kInterlacedScan
                                            : hci::PageScanType::kStandardScan);

  hci_cmd_runner_->QueueCommand(
      std::move(write_type), [self, interlaced](const hci::EventPacket& event) {
        if (BTEV_TEST_WARN(event,
                           "gap (BR/EDR): write page scan type failed")) {
          return;
        }
        if (!self) {
          return;
        }
        self->page_scan_type_ = (interlaced ? hci::PageScanType::kInterlacedScan
                                            : hci::PageScanType::kStandardScan);
        FXL_VLOG(2) << "gap (BR/EDR): page scan type updated";
      });

  hci_cmd_runner_->RunCommands(std::move(cb));
}

void BrEdrConnectionManager::OnConnectionRequest(
    const hci::EventPacket& event) {
  FXL_DCHECK(event.event_code() == hci::kConnectionRequestEventCode);
  const auto& params =
      event.view().payload<hci::ConnectionRequestEventParams>();
  std::string link_type_str =
      params.link_type == hci::LinkType::kACL ? "ACL" : "(e)SCO";
  FXL_VLOG(1) << "gap (BR/EDR): " << link_type_str << " conn request from "
              << params.bd_addr.ToString() << "("
              << params.class_of_device.ToString() << ")";
  if (params.link_type == hci::LinkType::kACL) {
    // Accept the connection, performing a role switch. We receive a
    // Connection Complete event when the connection is complete, and finish
    // the link then.
    FXL_LOG(INFO) << "gap (BR/EDR): accept incoming connection";

    auto accept = hci::CommandPacket::New(
        hci::kAcceptConnectionRequest,
        sizeof(hci::AcceptConnectionRequestCommandParams));
    auto accept_params =
        accept->mutable_view()
            ->mutable_payload<hci::AcceptConnectionRequestCommandParams>();
    accept_params->bd_addr = params.bd_addr;
    accept_params->role = hci::ConnectionRole::kMaster;

    hci_->command_channel()->SendCommand(std::move(accept), dispatcher_,
                                         nullptr, hci::kCommandStatusEventCode);
    return;
  }

  // Reject this connection.
  FXL_LOG(INFO) << "gap (BR/EDR): reject unsupported connection";

  auto reject = hci::CommandPacket::New(
      hci::kRejectConnectionRequest,
      sizeof(hci::RejectConnectionRequestCommandParams));
  auto reject_params =
      reject->mutable_view()
          ->mutable_payload<hci::RejectConnectionRequestCommandParams>();
  reject_params->bd_addr = params.bd_addr;
  reject_params->reason = hci::StatusCode::kConnectionRejectedBadBdAddr;

  hci_->command_channel()->SendCommand(std::move(reject), dispatcher_, nullptr,
                                       hci::kCommandStatusEventCode);
}

void BrEdrConnectionManager::OnConnectionComplete(
    const hci::EventPacket& event) {
  FXL_DCHECK(event.event_code() == hci::kConnectionCompleteEventCode);
  const auto& params =
      event.view().payload<hci::ConnectionCompleteEventParams>();
  FXL_VLOG(1) << "gap (BR/EDR): " << params.bd_addr.ToString()
              << fxl::StringPrintf(
                     " connection complete (status: 0x%02x handle: 0x%04x)",
                     params.status, params.connection_handle);
  if (BTEV_TEST_WARN(event, "gap (BR/EDR):  connection error")) {
    return;
  }
  common::DeviceAddress addr(common::DeviceAddress::Type::kBREDR,
                             params.bd_addr);

  // TODO(jamuraa): support non-master connections.
  auto conn_ptr = hci::Connection::CreateACL(
      params.connection_handle, hci::Connection::Role::kMaster,
      common::DeviceAddress(),  // TODO(armansito): Pass local BD_ADDR here.
      addr, hci_);

  if (params.link_type != hci::LinkType::kACL) {
    // Drop the connection if we don't support it.
    return;
  }

  RemoteDevice* device = cache_->FindDeviceByAddress(addr);
  if (!device) {
    device = cache_->NewDevice(addr, true);
  }
  // Interrogate this device to find out it's version/capabilities.
  interrogator_.Start(
      device->identifier(), std::move(conn_ptr),
      [device, self = weak_ptr_factory_.GetWeakPtr()](auto status,
                                                      auto conn_ptr) {
        if (BT_TEST_WARN(
                status,
                "gap (BR/EDR): interrogate failed, dropping connection")) {
          return;
        }

        self->connections_.emplace(device->identifier(), std::move(conn_ptr));
        // TODO(NET-406, NET-407): set up the L2CAP signalling channel and
        // start SDP service discovery.
      });
}

void BrEdrConnectionManager::OnDisconnectionComplete(
    const hci::EventPacket& event) {
  FXL_DCHECK(event.event_code() == hci::kDisconnectionCompleteEventCode);
  const auto& params =
      event.view().payload<hci::DisconnectionCompleteEventParams>();

  hci::ConnectionHandle handle = le16toh(params.connection_handle);
  if (BTEV_TEST_WARN(
          event,
          fxl::StringPrintf(
              "gap (BR/EDR): HCI disconnection error handle 0x%04x", handle))) {
    return;
  }

  auto it = std::find_if(
      connections_.begin(), connections_.end(),
      [handle](const auto& p) { return (p.second->handle() == handle); });

  if (it == connections_.end()) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "gap (BR/EDR): disconnect from unknown handle 0x%04x", handle);
    return;
  }
  std::string device = it->first;
  auto conn = std::move(it->second);
  connections_.erase(it);

  FXL_LOG(INFO) << fxl::StringPrintf(
      "gap (BR/EDR): %s disconnected - %s, handle: 0x%04x, reason: 0x%02x",
      device.c_str(), event.ToStatus().ToString().c_str(), handle,
      params.reason);

  // TODO(NET-406): Inform L2CAP that the connection has been disconnected.

  // Connection is already closed, so we don't need to send a disconnect.
  conn->set_closed();
}

}  // namespace gap
}  // namespace btlib
