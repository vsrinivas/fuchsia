// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bredr_discovery_manager.h"

#include <lib/async/default.h>

#include "garnet/drivers/bluetooth/lib/gap/remote_device_cache.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"
#include "garnet/drivers/bluetooth/lib/hci/util.h"
#include "lib/fxl/functional/auto_call.h"

namespace btlib {
namespace gap {

BrEdrDiscoverySession::BrEdrDiscoverySession(
    fxl::WeakPtr<BrEdrDiscoveryManager> manager)
    : manager_(manager) {}

BrEdrDiscoverySession::~BrEdrDiscoverySession() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  manager_->RemoveDiscoverySession(this);
}

void BrEdrDiscoverySession::NotifyDiscoveryResult(
    const RemoteDevice& device) const {
  if (device_found_callback_) {
    device_found_callback_(device);
  }
}

void BrEdrDiscoverySession::NotifyError() const {
  if (error_callback_) {
    error_callback_();
  }
}

BrEdrDiscoverableSession::BrEdrDiscoverableSession(
    fxl::WeakPtr<BrEdrDiscoveryManager> manager)
    : manager_(manager) {}

BrEdrDiscoverableSession::~BrEdrDiscoverableSession() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  manager_->RemoveDiscoverableSession(this);
}

BrEdrDiscoveryManager::BrEdrDiscoveryManager(fxl::RefPtr<hci::Transport> hci,
                                             RemoteDeviceCache* device_cache)
    : hci_(hci),
      dispatcher_(async_get_default()),
      cache_(device_cache),
      result_handler_id_(0u),
      weak_ptr_factory_(this) {
  FXL_DCHECK(cache_);
  FXL_DCHECK(hci_);
  FXL_DCHECK(dispatcher_);

  result_handler_id_ = hci_->command_channel()->AddEventHandler(
      hci::kInquiryResultEventCode,
      fit::bind_member(this, &BrEdrDiscoveryManager::InquiryResult),
      dispatcher_);
  FXL_DCHECK(result_handler_id_);
  // TODO(NET-729): add event handlers for the other inquiry modes
}

BrEdrDiscoveryManager::~BrEdrDiscoveryManager() {
  hci_->command_channel()->RemoveEventHandler(result_handler_id_);
  InvalidateDiscoverySessions();
}

void BrEdrDiscoveryManager::RequestDiscovery(DiscoveryCallback callback) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(callback);

  FXL_LOG(INFO) << "gap (BR/EDR): RequestDiscovery";

  // If we're already waiting on a callback, then scanning is already starting.
  // Queue this to create a session when the scanning starts.
  if (!pending_discovery_.empty()) {
    FXL_VLOG(1) << "gap (BR/EDR): discovery starting, add to pending";
    pending_discovery_.push(std::move(callback));
    return;
  }

  // If we're already scanning, just add a session.
  if (!discovering_.empty()) {
    FXL_VLOG(1) << "gap (BR/EDR): add to active sessions";
    auto session = AddDiscoverySession();
    callback(hci::Status(), std::move(session));
    return;
  }

  pending_discovery_.push(std::move(callback));
  MaybeStartInquiry();
}

// Starts the inquiry procedure if any sessions exist or are waiting to start.
void BrEdrDiscoveryManager::MaybeStartInquiry() {
  if (pending_discovery_.empty() && discovering_.empty()) {
    FXL_VLOG(1) << "gap (BR/EDR): no sessions, not starting inquiry";
    return;
  }
  FXL_VLOG(1) << "gap (BR/EDR): starting inqiury";

  auto inquiry =
      hci::CommandPacket::New(hci::kInquiry, sizeof(hci::InquiryCommandParams));
  auto params =
      inquiry->mutable_view()->mutable_payload<hci::InquiryCommandParams>();
  params->lap = hci::kGIAC;
  params->inquiry_length = hci::kInquiryLengthMax;
  params->num_responses = 0;
  hci_->command_channel()->SendCommand(
      std::move(inquiry), dispatcher_,
      [self = weak_ptr_factory_.GetWeakPtr()](auto, const auto& event) {
        if (!self) {
          return;
        }
        auto status = event.ToStatus();
        if (BT_TEST_WARN(status, "gap (BR/EDR): inquiry failure")) {
          // Failure of some kind, signal error to the sessions.
          self->InvalidateDiscoverySessions();
          // Fallthrough for callback to pending sessions.
        }

        // Resolve the request if the controller sent back a Command Complete or
        // Status event.
        // TODO(NET-770): Make it impossible for Command Complete to happen here
        // and remove handling for it.
        if (event.event_code() == hci::kCommandStatusEventCode ||
            event.event_code() == hci::kCommandCompleteEventCode) {
          // Inquiry started, make sessions for our waiting callbacks.
          while (!self->pending_discovery_.empty()) {
            auto callback = std::move(self->pending_discovery_.front());
            self->pending_discovery_.pop();
            callback(status, (status ? self->AddDiscoverySession() : nullptr));
          }
          return;
        }

        FXL_DCHECK(event.event_code() == hci::kInquiryCompleteEventCode);

        if (BT_TEST_VLOG(status, 2, "gap: inquiry complete failure")) {
          return;
        }

        FXL_VLOG(1) << "gap (BR/EDR): inquiry complete, restart";
        // We've stopped scanning because we timed out.
        self->MaybeStartInquiry();
      },
      hci::kInquiryCompleteEventCode);
}

// Stops the inquiry procedure.
void BrEdrDiscoveryManager::StopInquiry() {
  FXL_DCHECK(result_handler_id_);
  FXL_VLOG(2) << "gap (BR/EDR): cancelling inquiry";

  auto inq_cancel = hci::CommandPacket::New(hci::kInquiryCancel);
  hci_->command_channel()->SendCommand(
      std::move(inq_cancel), dispatcher_, [](long, const auto& event) {
        BTEV_TEST_WARN(event, "gap (BR/EDR): InquiryCancel failed");
      });
}

void BrEdrDiscoveryManager::InquiryResult(const hci::EventPacket& event) {
  FXL_DCHECK(event.event_code() == hci::kInquiryResultEventCode);

  FXL_VLOG(2) << "gap: BrEdrDiscoveryManager: InquiryResult received";

  if ((event.view().payload_size() - sizeof(hci::InquiryResultEventParams)) %
          sizeof(hci::InquiryResult) !=
      0) {
    FXL_LOG(INFO) << "gap (BR/EDR): ignoring malformed result ("
                  << event.view().payload_size() << " bytes)";
    return;
  }
  const auto& result = event.view().payload<hci::InquiryResultEventParams>();
  for (int i = 0; i < result.num_responses; i++) {
    common::DeviceAddress addr(common::DeviceAddress::Type::kBREDR,
                               result.responses[i].bd_addr);
    RemoteDevice* device = cache_->FindDeviceByAddress(addr);
    if (!device) {
      device = cache_->NewDevice(addr, true);
    }
    FXL_DCHECK(device);

    device->SetInquiryData(result.responses[i]);

    for (const auto& session : discovering_) {
      session->NotifyDiscoveryResult(*device);
    }
  }
}

void BrEdrDiscoveryManager::RequestDiscoverable(DiscoverableCallback callback) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(callback);

  FXL_LOG(INFO) << "gap (BR/EDR): RequestDiscoverable";

  auto self = weak_ptr_factory_.GetWeakPtr();
  auto status_cb = [self, cb = callback.share()](const auto& status) {
    cb(status, (status ? self->AddDiscoverableSession() : nullptr));
  };

  if (!pending_discoverable_.empty()) {
    FXL_VLOG(1) << "gap (BR/EDR): discovering starting, add to pending";
    pending_discoverable_.push(std::move(status_cb));
    return;
  }

  // If we're already discoverable, just add a session.
  if (!discoverable_.empty()) {
    FXL_VLOG(1) << "gap (BR/EDR): add to active discoverable";
    auto session = AddDiscoverableSession();
    callback(hci::Status(), std::move(session));
    return;
  }

  pending_discoverable_.push(std::move(status_cb));
  SetInquiryScan();
}

void BrEdrDiscoveryManager::SetInquiryScan() {
  bool enable = !discoverable_.empty() || !pending_discoverable_.empty();
  FXL_VLOG(2) << "gap (BR/EDR): " << (enable ? "enabling" : "disabling")
              << " inquiry scan ";

  auto self = weak_ptr_factory_.GetWeakPtr();
  auto scan_enable_cb = [self](auto, const hci::EventPacket& event) {
    if (!self) {
      return;
    }

    auto status = event.ToStatus();
    auto resolve_pending = fxl::MakeAutoCall([self, &status]() {
      while (!self->pending_discoverable_.empty()) {
        auto cb = std::move(self->pending_discoverable_.front());
        self->pending_discoverable_.pop();
        cb(status);
      }
    });

    if (BT_TEST_WARN(status, "gap (BR/EDR): Read Scan Enable failed: ")) {
      return;
    }
    bool enable =
        !self->discoverable_.empty() || !self->pending_discoverable_.empty();
    auto params = event.return_params<hci::ReadScanEnableReturnParams>();
    uint8_t scan_type = params->scan_enable;
    bool enabled =
        scan_type & static_cast<uint8_t>(hci::ScanEnableBit::kInquiry);

    if (enable == enabled) {
      FXL_LOG(INFO) << "gap (BR/EDR): inquiry scan already "
                    << (enable ? "enabled" : "disabled");
      return;
    }

    if (enable) {
      scan_type |= static_cast<uint8_t>(hci::ScanEnableBit::kInquiry);
    } else {
      scan_type &= ~static_cast<uint8_t>(hci::ScanEnableBit::kInquiry);
    }
    auto write_enable = hci::CommandPacket::New(
        hci::kWriteScanEnable, sizeof(hci::WriteScanEnableCommandParams));
    write_enable->mutable_view()
        ->mutable_payload<hci::WriteScanEnableCommandParams>()
        ->scan_enable = scan_type;
    resolve_pending.cancel();
    self->hci_->command_channel()->SendCommand(
        std::move(write_enable), self->dispatcher_,
        [self, enable](auto, const hci::EventPacket& event) {
          if (!self) {
            return;
          }
          BTEV_TEST_WARN(event, "gap (BR/EDR): Write Scan Enable failed");
          while (!self->pending_discoverable_.empty()) {
            auto cb = std::move(self->pending_discoverable_.front());
            self->pending_discoverable_.pop();
            cb(event.ToStatus());
          }
        });
  };

  auto read_enable = hci::CommandPacket::New(hci::kReadScanEnable);
  hci_->command_channel()->SendCommand(std::move(read_enable), dispatcher_,
                                       std::move(scan_enable_cb));
}

std::unique_ptr<BrEdrDiscoverySession>
BrEdrDiscoveryManager::AddDiscoverySession() {
  FXL_VLOG(2) << "gap (BR/EDR): adding discovery session";
  // Cannot use make_unique here since BrEdrDiscoverySession has a private
  // constructor.
  std::unique_ptr<BrEdrDiscoverySession> session(
      new BrEdrDiscoverySession(weak_ptr_factory_.GetWeakPtr()));
  FXL_DCHECK(discovering_.find(session.get()) == discovering_.end());
  discovering_.insert(session.get());
  return session;
}

void BrEdrDiscoveryManager::RemoveDiscoverySession(
    BrEdrDiscoverySession* session) {
  FXL_VLOG(2) << "gap (BR/EDR): removing discovery session";
  discovering_.erase(session);
  // TODO(jamuraa): When NET-619 is finished, cancel the running inquiry
  // With StopInquiry();
}

std::unique_ptr<BrEdrDiscoverableSession>
BrEdrDiscoveryManager::AddDiscoverableSession() {
  FXL_VLOG(2) << "gap (BR/EDR): adding discoverable session";
  // Cannot use make_unique here since BrEdrDiscoverableSession has a private
  // constructor.
  std::unique_ptr<BrEdrDiscoverableSession> session(
      new BrEdrDiscoverableSession(weak_ptr_factory_.GetWeakPtr()));
  FXL_DCHECK(discoverable_.find(session.get()) == discoverable_.end());
  discoverable_.insert(session.get());
  return session;
}

void BrEdrDiscoveryManager::RemoveDiscoverableSession(
    BrEdrDiscoverableSession* session) {
  FXL_VLOG(2) << "gap (BR/EDR): removing discoverable session";
  discoverable_.erase(session);
  if (discoverable_.empty()) {
    SetInquiryScan();
  }
}

void BrEdrDiscoveryManager::InvalidateDiscoverySessions() {
  for (auto session : discovering_) {
    session->NotifyError();
  }
  discovering_.clear();
}

}  // namespace gap
}  // namespace btlib
