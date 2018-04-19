// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bredr_discovery_manager.h"

#include <lib/async/default.h>

#include "garnet/drivers/bluetooth/lib/hci/transport.h"
#include "garnet/drivers/bluetooth/lib/hci/util.h"

#include "remote_device_cache.h"

namespace btlib {
namespace gap {

BrEdrDiscoverySession::BrEdrDiscoverySession(
    fxl::WeakPtr<BrEdrDiscoveryManager> manager)
    : manager_(manager) {}

BrEdrDiscoverySession::~BrEdrDiscoverySession() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  manager_->RemoveSession(this);
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
      fbl::BindMember(this, &BrEdrDiscoveryManager::InquiryResult),
      dispatcher_);
  FXL_DCHECK(result_handler_id_);
  // TODO(NET-729): add event handlers for the other inquiry modes
}

BrEdrDiscoveryManager::~BrEdrDiscoveryManager() {
  hci_->command_channel()->RemoveEventHandler(result_handler_id_);
  InvalidateSessions();
}

void BrEdrDiscoveryManager::RequestDiscovery(const SessionCallback& callback) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(callback);

  FXL_LOG(INFO) << "gap: BrEdrDiscoveryManager: RequestDiscovery";

  // If we're already waiting on a callback, then scanning is already starting.
  // Queue this to create a session when the scanning starts.
  if (!pending_.empty()) {
    FXL_VLOG(1) << "gap: BrEdrDiscoveryManager: starting, add to pending";
    pending_.push(callback);
    return;
  }

  // If we're already scanning, just add a session.
  if (!sessions_.empty()) {
    FXL_VLOG(1) << "gap: BrEdrDiscoveryManager: add to active sessions";
    auto session = AddSession();
    callback(hci::Status(), std::move(session));
    return;
  }

  pending_.push(callback);
  MaybeStartInquiry();
}

// Starts the inquiry procedure if any sessions exist or are waiting to start.
void BrEdrDiscoveryManager::MaybeStartInquiry() {
  if (pending_.empty() && sessions_.empty()) {
    FXL_VLOG(1)
        << "gap: BrEdrDiscoveryManager: no sessions, not starting inquiry";
    return;
  }
  FXL_VLOG(1) << "gap: BrEdrDiscoveryManager: starting inqiury";

  auto inquiry =
      hci::CommandPacket::New(hci::kInquiry, sizeof(hci::InquiryCommandParams));
  auto params =
      inquiry->mutable_view()->mutable_payload<hci::InquiryCommandParams>();
  params->lap = hci::kGIAC;
  params->inquiry_length = hci::kInquiryLengthMax;
  hci_->command_channel()->SendCommand(
      std::move(inquiry), dispatcher_,
      [self = weak_ptr_factory_.GetWeakPtr()](auto, const auto& event) {
        if (!self) {
          return;
        }
        auto status = event.ToStatus();
        if (!status) {
          // Failure of some kind, signal error to the sessions.
          FXL_LOG(WARNING) << "gap: BrEdrDiscoveryManager: inquiry failure: "
                           << status.ToString();
          self->InvalidateSessions();
          // Fallthrough for callback to pending sessions.
        }

        // Resolve the request if the controller sent back a Command Complete or
        // Status event.
        // TODO(NET-770): Make it impossible for Command Complete to happen here
        // and remove handling for it.
        if (event.event_code() == hci::kCommandStatusEventCode ||
            event.event_code() == hci::kCommandCompleteEventCode) {
          // Inquiry started, make sessions for our waiting callbacks.
          while (!self->pending_.empty()) {
            auto callback = self->pending_.front();
            callback(status, (status ? self->AddSession() : nullptr));
            self->pending_.pop();
          }
          return;
        }

        FXL_DCHECK(event.event_code() == hci::kInquiryCompleteEventCode);

        if (!status) {
          FXL_VLOG(1) << "gap: inquiry command failed";
          return;
        }

        // We've stopped scanning because we timed out.
        self->MaybeStartInquiry();
      },
      hci::kInquiryCompleteEventCode);
}

// Stops the inquiry procedure.
void BrEdrDiscoveryManager::StopInquiry() {
  FXL_DCHECK(result_handler_id_);
  FXL_VLOG(2) << "gap: BrEdrDiscoveryManager: cancelling inquiry";

  auto inq_cancel = hci::CommandPacket::New(hci::kInquiryCancel);
  hci_->command_channel()->SendCommand(
      std::move(inq_cancel), dispatcher_, [](long, const auto& event) {
        auto status = event.ToStatus();
        if (!status) {
          FXL_LOG(WARNING)
              << "gap: BrEdrDiscoveryManager: InquiryCancel failed: "
              << status.ToString();
        }
      });
}

void BrEdrDiscoveryManager::InquiryResult(const hci::EventPacket& event) {
  FXL_DCHECK(event.event_code() == hci::kInquiryResultEventCode);

  FXL_VLOG(2) << "gap: BrEdrDiscoveryManager: InquiryResult received";

  if ((event.view().payload_size() - sizeof(hci::InquiryResultEventParams)) %
          sizeof(hci::InquiryResult) !=
      0) {
    FXL_LOG(INFO) << "gap: BrEdrDiscoveryManager: ignoring malformed result ("
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

    for (const auto& session : sessions_) {
      session->NotifyDiscoveryResult(*device);
    }
  }
}

std::unique_ptr<BrEdrDiscoverySession> BrEdrDiscoveryManager::AddSession() {
  FXL_VLOG(2) << "gap: BrEdrDiscoveryManager: adding session";
  // Cannot use make_unique here since BrEdrDiscoverySession has a private
  // constructor.
  std::unique_ptr<BrEdrDiscoverySession> session(
      new BrEdrDiscoverySession(weak_ptr_factory_.GetWeakPtr()));
  FXL_DCHECK(sessions_.find(session.get()) == sessions_.end());
  sessions_.insert(session.get());
  return session;
}

void BrEdrDiscoveryManager::RemoveSession(BrEdrDiscoverySession* session) {
  FXL_VLOG(2) << "gap: BrEdrDiscoveryManager: removing session";
  sessions_.erase(session);
  // TODO(jamuraa): When NET-619 is finished, cancel the running inquiry
  // With StopInquiry();
}

void BrEdrDiscoveryManager::InvalidateSessions() {
  for (auto session : sessions_) {
    session->NotifyError();
  }
  sessions_.clear();
}

}  // namespace gap
}  // namespace btlib
