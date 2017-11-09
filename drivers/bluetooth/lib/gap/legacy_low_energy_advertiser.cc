// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "legacy_low_energy_advertiser.h"

#include <endian.h>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/gap/low_energy_connection_manager.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"
#include "lib/fsl/tasks/message_loop.h"

namespace bluetooth {
namespace gap {

namespace {

// Helpers for building HCI command packets:

constexpr size_t kFlagsSize = 3;
constexpr uint8_t kDefaultFlags = 0;

// Write the block for the flags to the |buffer|.
void WriteFlags(common::MutableByteBuffer* buffer, bool limited) {
  FXL_CHECK(buffer->size() >= kFlagsSize);
  (*buffer)[0] = 2;
  (*buffer)[1] = static_cast<uint8_t>(DataType::kFlags);
  if (limited) {
    (*buffer)[2] = kDefaultFlags | AdvFlag::kLELimitedDiscoverableMode;
  } else {
    (*buffer)[2] = kDefaultFlags | AdvFlag::kLEGeneralDiscoverableMode;
  }
}

std::unique_ptr<hci::CommandPacket> BuildEnablePacket(
    hci::GenericEnableParam enable) {
  constexpr size_t kPayloadSize =
      sizeof(hci::LESetAdvertisingEnableCommandParams);
  auto packet =
      hci::CommandPacket::New(hci::kLESetAdvertisingEnable, kPayloadSize);
  packet->mutable_view()
      ->mutable_payload<hci::LESetAdvertisingEnableCommandParams>()
      ->advertising_enable = enable;
  FXL_CHECK(packet);
  return packet;
}

std::unique_ptr<hci::CommandPacket> BuildSetAdvertisingData(
    const AdvertisingData& data) {
  auto packet =
      hci::CommandPacket::New(hci::kLESetAdvertisingData,
                              sizeof(hci::LESetAdvertisingDataCommandParams));
  packet->mutable_view()->mutable_payload_data().SetToZeros();

  auto params = packet->mutable_view()
                    ->mutable_payload<hci::LESetAdvertisingDataCommandParams>();
  params->adv_data_length = data.CalculateBlockSize() + kFlagsSize;

  common::MutableBufferView adv_view(params->adv_data, params->adv_data_length);
  auto flags_view = adv_view.mutable_view(0, kFlagsSize);
  WriteFlags(&flags_view, false);
  auto data_view = adv_view.mutable_view(kFlagsSize);
  data.WriteBlock(&data_view);

  return packet;
}

std::unique_ptr<hci::CommandPacket> BuildSetScanResponse(
    const AdvertisingData& scan_rsp) {
  auto packet =
      hci::CommandPacket::New(hci::kLESetScanResponseData,
                              sizeof(hci::LESetScanResponseDataCommandParams));
  packet->mutable_view()->mutable_payload_data().SetToZeros();

  auto params =
      packet->mutable_view()
          ->mutable_payload<hci::LESetScanResponseDataCommandParams>();
  params->scan_rsp_data_length = scan_rsp.CalculateBlockSize();

  common::MutableBufferView scan_data_view(params->scan_rsp_data,
                                           sizeof(params->scan_rsp_data));
  scan_rsp.WriteBlock(&scan_data_view);

  return packet;
}

std::unique_ptr<hci::CommandPacket> BuildSetRandomAddress(
    const common::DeviceAddress& address) {
  auto packet = hci::CommandPacket::New(
      hci::kLESetRandomAddress, sizeof(hci::LESetRandomAddressCommandParams));
  auto params = packet->mutable_view()
                    ->mutable_payload<hci::LESetRandomAddressCommandParams>();
  params->random_address = address.value();
  return packet;
}

std::unique_ptr<hci::CommandPacket> BuildSetAdvertisingParams(
    hci::LEAdvertisingType type,
    hci::LEOwnAddressType own_address_type,
    uint16_t interval_slices) {
  auto packet = hci::CommandPacket::New(
      hci::kLESetAdvertisingParameters,
      sizeof(hci::LESetAdvertisingParametersCommandParams));
  packet->mutable_view()->mutable_payload_data().SetToZeros();

  // Cap the advertising interval based on the allowed range
  // (Vol 2, Part E, 7.8.5)
  if (interval_slices > hci::kLEAdvertisingIntervalMax) {
    interval_slices = hci::kLEAdvertisingIntervalMax;
  } else if (interval_slices < hci::kLEAdvertisingIntervalMin) {
    interval_slices = hci::kLEAdvertisingIntervalMin;
  }

  auto params =
      packet->mutable_view()
          ->mutable_payload<hci::LESetAdvertisingParametersCommandParams>();
  params->adv_interval_min = htole16(interval_slices);
  params->adv_interval_max = htole16(interval_slices);
  params->adv_type = type;
  params->own_address_type = own_address_type;
  params->adv_channel_map = hci::kLEAdvertisingChannelAll;
  params->adv_filter_policy = hci::LEAdvFilterPolicy::kAllowAll;

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

LegacyLowEnergyAdvertiser::LegacyLowEnergyAdvertiser(
    fxl::RefPtr<hci::Transport> hci)
    : hci_(hci), connect_callback_(nullptr) {
  hci_cmd_runner_ = std::make_unique<hci::SequentialCommandRunner>(
      fsl::MessageLoop::GetCurrent()->task_runner(), hci_);
}

LegacyLowEnergyAdvertiser::~LegacyLowEnergyAdvertiser() {
  StopAdvertisingInternal();
}

size_t LegacyLowEnergyAdvertiser::GetSizeLimit() {
  // need space for the flags
  return hci::kMaxLEAdvertisingDataLength - kFlagsSize;
}

void LegacyLowEnergyAdvertiser::StartAdvertising(
    const bluetooth::common::DeviceAddress& address,
    const AdvertisingData& data,
    const AdvertisingData& scan_rsp,
    const ConnectionCallback& connect_callback,
    uint32_t interval_ms,
    bool anonymous,
    const AdvertisingResultCallback& callback) {
  // TODO(armansito): Handle the case when this gets called while a request to
  // start advertising is already pending.
  FXL_DCHECK(callback);
  FXL_DCHECK(address.type() != common::DeviceAddress::Type::kBREDR);

  if (anonymous) {
    FXL_VLOG(1) << "gap: LegacyLowEnergyAdvertiser: anonymous advertising not "
                   "supported";
    callback(0, hci::kUnsupportedFeatureOrParameter);
    return;
  }

  if (advertised_ != common::DeviceAddress()) {
    FXL_VLOG(1) << "gap: LegacyLowEnergyAdvertiser: already advertising";
    callback(0, hci::kUnsupportedFeatureOrParameter);
    return;
  }

  if (advertised_ != common::DeviceAddress()) {
    callback(0, hci::kConnectionLimitExceeded);
    return;
  }

  if (data.CalculateBlockSize() > GetSizeLimit()) {
    FXL_VLOG(1) << "gap: LegacyLowEnergyAdvertiser: advertising data too large";
    callback(0, hci::kMemoryCapacityExceeded);
    return;
  }

  if (scan_rsp.CalculateBlockSize() > GetSizeLimit()) {
    FXL_VLOG(1) << "gap: LegacyLowEnergyAdvertiser: scan response too large";
    callback(0, hci::kMemoryCapacityExceeded);
    return;
  }

  // Set advertising and scan response data. If either data is empty then it
  // will be cleared accordingly.
  hci_cmd_runner_->QueueCommand(BuildSetAdvertisingData(data));
  hci_cmd_runner_->QueueCommand(BuildSetScanResponse(scan_rsp));

  // Set random address
  if (address.type() != common::DeviceAddress::Type::kLEPublic) {
    hci_cmd_runner_->QueueCommand(BuildSetRandomAddress(address));
  }

  // Set advertising parameters
  uint16_t interval_slices = MillisecondsToTimeslices(interval_ms);
  hci::LEAdvertisingType type = hci::LEAdvertisingType::kAdvNonConnInd;
  if (connect_callback) {
    type = hci::LEAdvertisingType::kAdvInd;
  } else if (scan_rsp.CalculateBlockSize() > 0) {
    type = hci::LEAdvertisingType::kAdvScanInd;
  }

  hci::LEOwnAddressType own_addr_type;
  if (address.type() == common::DeviceAddress::Type::kLEPublic) {
    own_addr_type = hci::LEOwnAddressType::kPublic;
  } else {
    own_addr_type = hci::LEOwnAddressType::kRandom;
  }

  hci_cmd_runner_->QueueCommand(
      BuildSetAdvertisingParams(type, own_addr_type, interval_slices));

  // Enable advertising.
  hci_cmd_runner_->QueueCommand(
      BuildEnablePacket(hci::GenericEnableParam::kEnable));

  hci_cmd_runner_->RunCommands([this, address, interval_slices, callback,
                                connect_callback](bool success) {
    if (success) {
      advertised_ = address;
      connect_callback_ = connect_callback;
      callback(TimeslicesToMilliseconds(interval_slices), hci::kSuccess);
    } else {
      // Clear out the advertising data if it partially succeeded.
      StopAdvertisingInternal();
      callback(0, hci::kUnspecifiedError);
    }
  });
}

bool LegacyLowEnergyAdvertiser::StopAdvertising(
    const bluetooth::common::DeviceAddress& address) {
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
    if (advertised_ != common::DeviceAddress()) {
      // Pending stop running, nothing to do here.
      return;
    }
    // Cancel a pending start
    hci_cmd_runner_->Cancel();
  }

  // Disable advertising
  hci_cmd_runner_->QueueCommand(
      BuildEnablePacket(hci::GenericEnableParam::kDisable));

  // Unset advertising data
  auto data_packet =
      hci::CommandPacket::New(hci::kLESetAdvertisingData,
                              sizeof(hci::LESetAdvertisingDataCommandParams));
  data_packet->mutable_view()->mutable_payload_data().SetToZeros();
  hci_cmd_runner_->QueueCommand(std::move(data_packet));

  // Set scan response data
  auto scan_rsp_packet =
      hci::CommandPacket::New(hci::kLESetScanResponseData,
                              sizeof(hci::LESetScanResponseDataCommandParams));
  scan_rsp_packet->mutable_view()->mutable_payload_data().SetToZeros();
  hci_cmd_runner_->QueueCommand(std::move(scan_rsp_packet));

  hci_cmd_runner_->RunCommands([this](bool) {
    // Even on failure, we want to consider us not advertising.
    advertised_ = common::DeviceAddress();
  });
}

void LegacyLowEnergyAdvertiser::OnIncomingConnection(
    LowEnergyConnectionRefPtr connection) {
  if (connect_callback_) {
    connect_callback_(std::move(connection));
  }
}

}  // namespace gap
}  // namespace bluetooth
