// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "connection.h"

#include <endian.h>

#include "apps/bluetooth/lib/hci/control_packets.h"
#include "apps/bluetooth/lib/hci/defaults.h"
#include "apps/bluetooth/lib/hci/transport.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace bluetooth {
namespace hci {
namespace {

std::string LinkTypeToString(Connection::LinkType type) {
  switch (type) {
    case Connection::LinkType::kACL:
      return "ACL";
    case Connection::LinkType::kSCO:
      return "SCO";
    case Connection::LinkType::kESCO:
      return "ESCO";
    case Connection::LinkType::kLE:
      return "LE";
  }

  FXL_NOTREACHED();
  return "(invalid)";
}

}  // namespace

Connection::LowEnergyParameters::LowEnergyParameters(uint16_t interval_min, uint16_t interval_max,
                                                     uint16_t interval, uint16_t latency,
                                                     uint16_t supervision_timeout)
    : interval_min_(interval_min),
      interval_max_(interval_max),
      interval_(interval),
      latency_(latency),
      supervision_timeout_(supervision_timeout) {
  FXL_DCHECK(interval_min_ <= interval_max_);
}

Connection::LowEnergyParameters::LowEnergyParameters()
    : interval_min_(defaults::kLEConnectionIntervalMin),
      interval_max_(defaults::kLEConnectionIntervalMax),
      interval_(0x0000),
      latency_(0x0000),
      supervision_timeout_(defaults::kLESupervisionTimeout) {}

bool Connection::LowEnergyParameters::operator==(const LowEnergyParameters& other) const {
  return other.interval_min_ == interval_min_ && other.interval_max_ == interval_max_ &&
         other.interval_ == interval_ && other.latency_ == latency_ &&
         other.supervision_timeout_ == supervision_timeout_;
}

Connection::Connection(ConnectionHandle handle, Role role,
                       const common::DeviceAddress& peer_address, const LowEnergyParameters& params,
                       fxl::RefPtr<Transport> hci)
    : ll_type_(LinkType::kLE),
      handle_(handle),
      role_(role),
      is_open_(true),
      peer_address_(peer_address),
      le_params_(params),
      hci_(hci),
      weak_ptr_factory_(this) {
  FXL_DCHECK(handle);
  FXL_DCHECK(le_params_.interval());
  FXL_DCHECK(hci_);
}

Connection::~Connection() {
  Close();
}

void Connection::MarkClosed() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(is_open_);
  is_open_ = false;
}

void Connection::Close(Status reason, const fxl::Closure& callback) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  if (!is_open()) return;

  // The connection is immediately marked as closed as there is no reasonable way for a Disconnect
  // procedure to fail, i.e. it always succeeds. If the controller reports failure in the
  // Disconnection Complete event, it should be because we gave it an already disconnected handle
  // which we would treat as success.
  //
  // TODO(armansito): The procedure could also fail if "the command was not presently allowed".
  // Retry in that case?
  is_open_ = false;

  FXL_DCHECK(!close_cb_);
  close_cb_ = callback;

  // Here we send a HCI_Disconnect command. We use a matcher so that this command completes when we
  // receive a HCI Disconnection Complete event with the connection handle that belongs to this
  // connection.

  auto matcher = [handle = handle_](const EventPacket& event)->bool {
    FXL_DCHECK(event.event_code() == kDisconnectionCompleteEventCode);
    return handle ==
           le16toh(event.view().payload<DisconnectionCompleteEventParams>().connection_handle);
  };

  auto self = weak_ptr_factory_.GetWeakPtr();

  auto status_cb = [self](auto id, Status status) {
    if (status != Status::kSuccess) {
      if (status == Status::kCommandTimeout && self) {
        self->NotifyClosed();
        return;
      }

      FXL_LOG(WARNING) << "Ignoring failed disconnect command status: 0x" << std::hex << status;
    }
  };

  auto complete_cb = [ handle = handle_, self ](auto id, const EventPacket& event) {
    FXL_DCHECK(event.event_code() == kDisconnectionCompleteEventCode);
    const auto& params = event.view().payload<DisconnectionCompleteEventParams>();
    FXL_DCHECK(handle == le16toh(params.connection_handle));

    if (params.status != Status::kSuccess) {
      FXL_LOG(WARNING) << "Ignoring failed disconnection status: 0x" << std::hex << params.status;
    }

    if (self) self->NotifyClosed();
  };

  auto disconn = CommandPacket::New(kDisconnect, sizeof(DisconnectCommandParams));
  auto params = disconn->mutable_view()->mutable_payload<DisconnectCommandParams>();
  params->connection_handle = htole16(handle_);
  params->reason = reason;

  hci_->command_channel()->SendCommand(std::move(disconn),
                                       mtl::MessageLoop::GetCurrent()->task_runner(), complete_cb,
                                       status_cb, kDisconnectionCompleteEventCode, matcher);
}

std::string Connection::ToString() const {
  return fxl::StringPrintf(
      "(%s link - handle: 0x%04x, role: %s, address: %s, "
      "interval: %.2f ms, latency: %.2f ms, timeout: %u ms)",
      LinkTypeToString(ll_type_).c_str(), handle_, role_ == Role::kMaster ? "master" : "slave",
      peer_address_.ToString().c_str(), static_cast<float>(le_params_.interval()) * 1.25f,
      static_cast<float>(le_params_.latency()) * 1.25f, le_params_.supervision_timeout() * 10u);
}

}  // namespace hci
}  // namespace bluetooth
