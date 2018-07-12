// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "legacy_low_energy_advertiser.h"

#include <endian.h>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"
#include "garnet/drivers/bluetooth/lib/hci/util.h"

namespace btlib {
namespace hci {

namespace {

// Helpers for building HCI command packets:

std::unique_ptr<CommandPacket> BuildEnablePacket(GenericEnableParam enable) {
  constexpr size_t kPayloadSize = sizeof(LESetAdvertisingEnableCommandParams);
  auto packet = CommandPacket::New(kLESetAdvertisingEnable, kPayloadSize);
  packet->mutable_view()
      ->mutable_payload<LESetAdvertisingEnableCommandParams>()
      ->advertising_enable = enable;
  FXL_CHECK(packet);
  return packet;
}

std::unique_ptr<CommandPacket> BuildSetAdvertisingData(
    const common::ByteBuffer& data) {
  auto packet = CommandPacket::New(kLESetAdvertisingData,
                                   sizeof(LESetAdvertisingDataCommandParams));
  packet->mutable_view()->mutable_payload_data().SetToZeros();

  auto params = packet->mutable_view()
                    ->mutable_payload<LESetAdvertisingDataCommandParams>();
  params->adv_data_length = data.size();

  common::MutableBufferView adv_view(params->adv_data, params->adv_data_length);
  data.Copy(&adv_view);

  return packet;
}

std::unique_ptr<CommandPacket> BuildSetScanResponse(
    const common::ByteBuffer& scan_rsp) {
  auto packet = CommandPacket::New(kLESetScanResponseData,
                                   sizeof(LESetScanResponseDataCommandParams));
  packet->mutable_view()->mutable_payload_data().SetToZeros();

  auto params = packet->mutable_view()
                    ->mutable_payload<LESetScanResponseDataCommandParams>();
  params->scan_rsp_data_length = scan_rsp.size();

  common::MutableBufferView scan_data_view(params->scan_rsp_data,
                                           sizeof(params->scan_rsp_data));
  scan_rsp.Copy(&scan_data_view);

  return packet;
}

std::unique_ptr<CommandPacket> BuildSetRandomAddress(
    const common::DeviceAddress& address) {
  auto packet = CommandPacket::New(kLESetRandomAddress,
                                   sizeof(LESetRandomAddressCommandParams));
  auto params = packet->mutable_view()
                    ->mutable_payload<LESetRandomAddressCommandParams>();
  params->random_address = address.value();
  return packet;
}

std::unique_ptr<CommandPacket> BuildSetAdvertisingParams(
    LEAdvertisingType type,
    LEOwnAddressType own_address_type,
    uint16_t interval_slices) {
  auto packet =
      CommandPacket::New(kLESetAdvertisingParameters,
                         sizeof(LESetAdvertisingParametersCommandParams));
  packet->mutable_view()->mutable_payload_data().SetToZeros();

  // Cap the advertising interval based on the allowed range
  // (Vol 2, Part E, 7.8.5)
  if (interval_slices > kLEAdvertisingIntervalMax) {
    interval_slices = kLEAdvertisingIntervalMax;
  } else if (interval_slices < kLEAdvertisingIntervalMin) {
    interval_slices = kLEAdvertisingIntervalMin;
  }

  auto params =
      packet->mutable_view()
          ->mutable_payload<LESetAdvertisingParametersCommandParams>();
  params->adv_interval_min = htole16(interval_slices);
  params->adv_interval_max = htole16(interval_slices);
  params->adv_type = type;
  params->own_address_type = own_address_type;
  params->adv_channel_map = kLEAdvertisingChannelAll;
  params->adv_filter_policy = LEAdvFilterPolicy::kAllowAll;

  // We don't support directed advertising yet, so leave peer_address as 0x00
  // (|packet| parameters are initialized to zero above).

  return packet;
}

// This function is undefined outside the range that ms is valid:
// notabaly at 40960 ms it will produce undefined values.
uint16_t MillisecondsToTimeslices(uint16_t ms) {
  return (uint16_t)(static_cast<uint32_t>(ms) * 1000 / 625);
}

uint16_t TimeslicesToMilliseconds(uint16_t timeslices) {
  // Promoted so we don't overflow
  return (uint16_t)(static_cast<uint32_t>(timeslices) * 625 / 1000);
}

}  // namespace

LegacyLowEnergyAdvertiser::LegacyLowEnergyAdvertiser(fxl::RefPtr<Transport> hci)
    : hci_(hci), starting_(false), connect_callback_(nullptr) {
  hci_cmd_runner_ = std::make_unique<SequentialCommandRunner>(
      async_get_default_dispatcher(), hci_);
}

LegacyLowEnergyAdvertiser::~LegacyLowEnergyAdvertiser() {
  StopAdvertisingInternal();
}

size_t LegacyLowEnergyAdvertiser::GetSizeLimit() {
  return kMaxLEAdvertisingDataLength;
}

void LegacyLowEnergyAdvertiser::StartAdvertising(
    const common::DeviceAddress& address,
    const common::ByteBuffer& data,
    const common::ByteBuffer& scan_rsp,
    ConnectionCallback connect_callback,
    uint32_t interval_ms,
    bool anonymous,
    AdvertisingStatusCallback callback) {
  FXL_DCHECK(callback);
  FXL_DCHECK(address.type() != common::DeviceAddress::Type::kBREDR);

  if (anonymous) {
    FXL_VLOG(1) << "hci: LegacyLowEnergyAdvertiser: anonymous advertising not "
                   "supported";
    callback(0, Status(common::HostError::kNotSupported));
    return;
  }

  if (advertising()) {
    if (address != advertised_) {
      FXL_VLOG(1) << "hci: LegacyLowEnergyAdvertiser: already advertising";
      callback(0, Status(common::HostError::kNotSupported));
      return;
    }
    FXL_VLOG(1)
        << "hci: LegacyLowEnergyAdvertiser: updating existing advertisement";
  }

  if (data.size() > GetSizeLimit()) {
    FXL_VLOG(1) << "hci: LegacyLowEnergyAdvertiser: advertising data too large";
    callback(0, Status(common::HostError::kInvalidParameters));
    return;
  }

  if (scan_rsp.size() > GetSizeLimit()) {
    FXL_VLOG(1) << "hci: LegacyLowEnergyAdvertiser: scan response too large";
    callback(0, Status(common::HostError::kInvalidParameters));
    return;
  }

  if (!hci_cmd_runner_->IsReady()) {
    if (starting_) {
      FXL_VLOG(1) << "hci: LegacyLowEnergyAdvertiser: already starting";
      callback(0, Status(common::HostError::kInProgress));
      return;
    }

    // Abort any remaining commands from the current stop sequence. If we got
    // here then the controller MUST receive our request to disable advertising,
    // so the commands that we send next will overwrite the current advertising
    // settings and re-enable it.
    hci_cmd_runner_->Cancel();
  }

  starting_ = true;

  if (advertising()) {
    // Temporarily disable advertising so we can tweak the parameters.
    hci_cmd_runner_->QueueCommand(
        BuildEnablePacket(GenericEnableParam::kDisable));
  }

  // Set advertising and scan response data. If either data is empty then it
  // will be cleared accordingly.
  hci_cmd_runner_->QueueCommand(BuildSetAdvertisingData(data));
  hci_cmd_runner_->QueueCommand(BuildSetScanResponse(scan_rsp));

  // Set random address
  if (!advertising() &&
      (address.type() != common::DeviceAddress::Type::kLEPublic)) {
    hci_cmd_runner_->QueueCommand(BuildSetRandomAddress(address));
  }

  // Set advertising parameters
  uint16_t interval_slices = MillisecondsToTimeslices(interval_ms);
  LEAdvertisingType type = LEAdvertisingType::kAdvNonConnInd;
  if (connect_callback) {
    type = LEAdvertisingType::kAdvInd;
  } else if (scan_rsp.size() > 0) {
    type = LEAdvertisingType::kAdvScanInd;
  }

  LEOwnAddressType own_addr_type;
  if (address.type() == common::DeviceAddress::Type::kLEPublic) {
    own_addr_type = LEOwnAddressType::kPublic;
  } else {
    own_addr_type = LEOwnAddressType::kRandom;
  }

  hci_cmd_runner_->QueueCommand(
      BuildSetAdvertisingParams(type, own_addr_type, interval_slices));

  // Enable advertising.
  hci_cmd_runner_->QueueCommand(BuildEnablePacket(GenericEnableParam::kEnable));

  hci_cmd_runner_->RunCommands([this, address, interval_slices,
                                callback = std::move(callback),
                                connect_callback = std::move(connect_callback)](Status status) mutable {
    FXL_DCHECK(starting_);
    starting_ = false;

    FXL_VLOG(1) << "gap: LegacyLowEnergyAdvertiser: advertising status: "
                << status.ToString();

    uint16_t interval;
    if (status) {
      advertised_ = address;
      connect_callback_ = std::move(connect_callback);
      interval = TimeslicesToMilliseconds(interval_slices);
    } else {
      // Clear out the advertising data if it partially succeeded.
      StopAdvertisingInternal();
      interval = 0;
    }

    callback(interval, status);
  });
}

bool LegacyLowEnergyAdvertiser::StopAdvertising(
    const common::DeviceAddress& address) {
  if (advertised_ != address) {
    // not advertising, or not on this address.
    return false;
  }
  StopAdvertisingInternal();
  return true;
}

void LegacyLowEnergyAdvertiser::StopAdvertisingInternal() {
  connect_callback_ = nullptr;

  if (!hci_cmd_runner_->IsReady()) {
    if (!starting_) {
      FXL_VLOG(1) << "hci: LegacyLowEnergyAdvertiser: already stopping";

      // The advertised address must have been cleared in this state.
      FXL_DCHECK(!advertising());
      return;
    }

    // Cancel the pending start
    FXL_DCHECK(starting_);
    hci_cmd_runner_->Cancel();
    starting_ = false;
  }

  // Even on failure, we want to consider us not advertising. Clear the
  // advertised address here so that new advertisements can be requested right
  // away.
  advertised_ = {};

  // Disable advertising
  hci_cmd_runner_->QueueCommand(
      BuildEnablePacket(GenericEnableParam::kDisable));

  // Unset advertising data
  auto data_packet = CommandPacket::New(
      kLESetAdvertisingData, sizeof(LESetAdvertisingDataCommandParams));
  data_packet->mutable_view()->mutable_payload_data().SetToZeros();
  hci_cmd_runner_->QueueCommand(std::move(data_packet));

  // Set scan response data
  auto scan_rsp_packet = CommandPacket::New(
      kLESetScanResponseData, sizeof(LESetScanResponseDataCommandParams));
  scan_rsp_packet->mutable_view()->mutable_payload_data().SetToZeros();
  hci_cmd_runner_->QueueCommand(std::move(scan_rsp_packet));

  hci_cmd_runner_->RunCommands([](Status status) {
    FXL_VLOG(1) << "gap: LegacyLowEnergyAdvertiser: advertising stopped: "
                << status.ToString();
  });
}

void LegacyLowEnergyAdvertiser::OnIncomingConnection(ConnectionPtr link) {
  if (!advertising()) {
    FXL_VLOG(1) << "hci: LegacyLowEnergyAdvertiser: connection received "
                   "without advertising!";
    return;
  }

  if (!connect_callback_) {
    FXL_VLOG(1) << "hci: LegacyLowEnergyAdvertiser: connection received when "
                   "not connectable!";
    return;
  }

  auto callback = std::move(connect_callback_);
  StopAdvertisingInternal();

  callback(std::move(link));
}

}  // namespace hci
}  // namespace btlib
