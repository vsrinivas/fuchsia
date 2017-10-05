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

std::unique_ptr<hci::CommandPacket> AdvertisingEnablePacket(
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

const size_t kFlagsSize = 3;
const uint8_t kDefaultFlags = 0;

// Write the block for the flags to the |buffer|.
void WriteFlags(common::MutableByteBuffer *buffer, bool limited) {
  FXL_CHECK(buffer->size() >= kFlagsSize);
  (*buffer)[0] = 2;
  (*buffer)[1] = static_cast<uint8_t>(DataType::kFlags);
  if (limited) {
    (*buffer)[2] = kDefaultFlags | AdvFlag::kLELimitedDiscoverableMode;
  } else {
    (*buffer)[2] = kDefaultFlags | AdvFlag::kLEGeneralDiscoverableMode;
  }
}

// This function is undefined outside the range that ms is valid:
// notabaly at 40960 ms it will produce undefined values.
uint16_t MillisecondsToTimeslices(uint16_t ms) {
  return (uint16_t)(((uint32_t)ms * 1000) / 625);
}

uint16_t TimeslicesToMilliseconds(uint16_t timeslices) {
  // Promoted so we don't overflow
  return (uint16_t)((uint32_t)timeslices * 625) / 1000;
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

bool LegacyLowEnergyAdvertiser::StartAdvertising(
    const bluetooth::common::DeviceAddress& address,
    const AdvertisingData& data,
    const AdvertisingData& scan_rsp,
    const ConnectionCallback& connect_callback,
    uint32_t interval_ms,
    bool anonymous,
    const AdvertisingResultCallback& callback) {
  if ((advertised_ != common::DeviceAddress()) || anonymous) {
    callback(0, hci::kUnsupportedFeatureOrParameter);
    return false;
  }

  if (data.CalculateBlockSize() > GetSizeLimit()) {
    callback(0, hci::kInvalidHCICommandParameters);
    return false;
  }

  if (scan_rsp.CalculateBlockSize() > GetSizeLimit()) {
    callback(0, hci::kInvalidHCICommandParameters);
    return false;
  }

  hci::LEAdvertisingType type = hci::LEAdvertisingType::kAdvNonConnInd;

  // Set advertising data
  auto data_packet =
      hci::CommandPacket::New(hci::kLESetAdvertisingData,
                              sizeof(hci::LESetAdvertisingDataCommandParams));
  data_packet->mutable_view()->mutable_payload_data().SetToZeros();
  auto data_params =
      data_packet->mutable_view()
          ->mutable_payload<hci::LESetAdvertisingDataCommandParams>();
  data_params->adv_data_length = data.CalculateBlockSize() + kFlagsSize;
  common::MutableBufferView adv_view(data_params->adv_data, data_params->adv_data_length);
  auto flags_view = adv_view.mutable_view(0, kFlagsSize);
  WriteFlags(&flags_view, false);
  auto data_view = adv_view.mutable_view(kFlagsSize);
  data.WriteBlock(&data_view);
  hci_cmd_runner_->QueueCommand(std::move(data_packet));

  // Set scan response data
  auto scan_rsp_packet =
      hci::CommandPacket::New(hci::kLESetScanResponseData,
                              sizeof(hci::LESetScanResponseDataCommandParams));
  scan_rsp_packet->mutable_view()->mutable_payload_data().SetToZeros();
  auto scan_rsp_params =
      scan_rsp_packet->mutable_view()
          ->mutable_payload<hci::LESetScanResponseDataCommandParams>();
  scan_rsp_params->scan_rsp_data_length = scan_rsp.CalculateBlockSize();
  common::MutableBufferView scan_data_view(scan_rsp_params->scan_rsp_data,
                                        sizeof(scan_rsp_params->scan_rsp_data));
  scan_rsp.WriteBlock(&scan_data_view);
  hci_cmd_runner_->QueueCommand(std::move(scan_rsp_packet));

  if (scan_rsp.CalculateBlockSize() > 0) {
    type = hci::LEAdvertisingType::kAdvScanInd;
  }
  // Set random address
  if (address.type() != common::DeviceAddress::Type::kLEPublic) {
    auto address_packet = hci::CommandPacket::New(
        hci::kLESetRandomAddress, sizeof(hci::LESetRandomAddressCommandParams));
    auto address_params =
        address_packet->mutable_view()
            ->mutable_payload<hci::LESetRandomAddressCommandParams>();
    address_params->random_address = address.value();
    hci_cmd_runner_->QueueCommand(std::move(address_packet));
  }
  // Set advertising parameters
  auto param_packet = hci::CommandPacket::New(
      hci::kLESetAdvertisingParameters,
      sizeof(hci::LESetAdvertisingParametersCommandParams));
  param_packet->mutable_view()->mutable_payload_data().SetToZeros();
  auto param_params =
      param_packet->mutable_view()
          ->mutable_payload<hci::LESetAdvertisingParametersCommandParams>();
  uint16_t interval_timeslices = MillisecondsToTimeslices(interval_ms);
  if (interval_ms > TimeslicesToMilliseconds(hci::kLEAdvertisingIntervalMax)) {
    interval_timeslices = hci::kLEAdvertisingIntervalMax;
  } else if (interval_timeslices < hci::kLEAdvertisingIntervalMin) {
    interval_timeslices = hci::kLEAdvertisingIntervalMin;
  }

  param_params->adv_interval_min = htole16(interval_timeslices);
  param_params->adv_interval_max = htole16(interval_timeslices);
  if (connect_callback) {
    type = hci::LEAdvertisingType::kAdvInd;
  }
  param_params->adv_type = type;
  param_params->own_address_type = hci::LEOwnAddressType::kPublic;
  if (address.type() != common::DeviceAddress::Type::kLEPublic) {
    param_params->own_address_type = hci::LEOwnAddressType::kRandom;
  }
  // We don't support directed advertising yet, so leave peer_address as 0x00
  param_params->adv_channel_map = hci::kLEAdvertisingChannelAll;
  param_params->adv_filter_policy = hci::LEAdvFilterPolicy::kAllowAll;
  hci_cmd_runner_->QueueCommand(std::move(param_packet));

  hci_cmd_runner_->QueueCommand(
      AdvertisingEnablePacket(hci::GenericEnableParam::kEnable));

  hci_cmd_runner_->RunCommands([this, address, interval_timeslices, callback,
                                connect_callback](bool success) {
    if (success) {
      advertised_ = address;
      connect_callback_ = connect_callback;
      callback(TimeslicesToMilliseconds(interval_timeslices), hci::kSuccess);
    } else {
      // Clear out the advertising data if it partially succeeded.
      StopAdvertisingInternal();
      callback(0, hci::kUnspecifiedError);
    }
  });
  return true;
}

void LegacyLowEnergyAdvertiser::StopAdvertising(
    const bluetooth::common::DeviceAddress& address) {
  if (advertised_ != address) {
    // not advertising.
    return;
  }
  StopAdvertisingInternal();
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
      AdvertisingEnablePacket(hci::GenericEnableParam::kDisable));

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
