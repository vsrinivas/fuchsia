// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_device.h"

#include "garnet/drivers/bluetooth/lib/gap/advertising_data.h"
#include "garnet/drivers/bluetooth/lib/hci/low_energy_scanner.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "advertising_data.h"

namespace btlib {
namespace gap {
namespace {

std::string ConnectionStateToString(RemoteDevice::ConnectionState state) {
  switch (state) {
    case RemoteDevice::ConnectionState::kNotConnected:
      return "not connected";
    case RemoteDevice::ConnectionState::kInitializing:
      return "initializing";
    case RemoteDevice::ConnectionState::kConnected:
      return "initialized";
    case RemoteDevice::ConnectionState::kBonding:
      return "bonding";
    case RemoteDevice::ConnectionState::kBonded:
      return "bonded";
  }

  FXL_NOTREACHED();
  return "(unknown)";
}

constexpr uint16_t kClockOffsetValidBitMask = 0x8000;

}  // namespace

RemoteDevice::RemoteDevice(UpdatedCallback callback,
                           const std::string& identifier,
                           const common::DeviceAddress& address,
                           bool connectable)
    : updated_callback_(std::move(callback)),
      identifier_(identifier),
      address_(address),
      technology_((address.type() == common::DeviceAddress::Type::kBREDR)
                      ? TechnologyType::kClassic
                      : TechnologyType::kLowEnergy),
      le_connection_state_(ConnectionState::kNotConnected),
      bredr_connection_state_(ConnectionState::kNotConnected),
      connectable_(connectable),
      temporary_(true),
      rssi_(hci::kRSSIInvalid),
      advertising_data_length_(0u) {
  FXL_DCHECK(updated_callback_);
  FXL_DCHECK(!identifier_.empty());
  // TODO(armansito): Add a mechanism for assigning "dual-mode" for technology.
}

void RemoteDevice::set_le_connection_state(ConnectionState state) {
  FXL_DCHECK(connectable() || state == ConnectionState::kNotConnected);
  FXL_VLOG(1) << "gap: RemoteDevice le_connection_state changed from \""
              << ConnectionStateToString(le_connection_state_) << "\" to \""
              << ConnectionStateToString(state) << "\"";

  le_connection_state_ = state;
  updated_callback_(this);
}

void RemoteDevice::set_bredr_connection_state(ConnectionState state) {
  FXL_DCHECK(connectable() || state == ConnectionState::kNotConnected);
  FXL_VLOG(1) << "gap: RemoteDevice bredr_connection_state changed from \""
              << ConnectionStateToString(bredr_connection_state_) << "\" to \""
              << ConnectionStateToString(state) << "\"";

  bredr_connection_state_ = state;
  updated_callback_(this);
}

void RemoteDevice::SetLEAdvertisingData(
    int8_t rssi, const common::ByteBuffer& advertising_data) {
  FXL_DCHECK(technology() == TechnologyType::kLowEnergy);
  FXL_DCHECK(address_.type() != common::DeviceAddress::Type::kBREDR);

  // Reallocate the advertising data buffer only if we need more space.
  // TODO(armansito): Revisit this strategy while addressing NET-209
  if (advertising_data_buffer_.size() < advertising_data.size()) {
    advertising_data_buffer_ =
        common::DynamicByteBuffer(advertising_data.size());
  }

  AdvertisingData old_parsed_ad;
  if (!AdvertisingData::FromBytes(advertising_data_buffer_, &old_parsed_ad)) {
    old_parsed_ad = AdvertisingData();
  }

  AdvertisingData new_parsed_ad;
  if (!AdvertisingData::FromBytes(advertising_data, &new_parsed_ad)) {
    new_parsed_ad = AdvertisingData();
  }

  rssi_ = rssi;
  advertising_data_length_ = advertising_data.size();
  advertising_data.Copy(&advertising_data_buffer_);

  if (old_parsed_ad.local_name() != new_parsed_ad.local_name()) {
    updated_callback_(this);
  }
}

void RemoteDevice::SetExtendedInquiryResponse(const common::ByteBuffer& bytes) {
  FXL_DCHECK(bytes.size() <= hci::kExtendedInquiryResponseBytes);
  if (extended_inquiry_response_.size() < bytes.size()) {
    extended_inquiry_response_ = common::DynamicByteBuffer(bytes.size());
  }
  bytes.Copy(&extended_inquiry_response_);

  // TODO(jamuraa): maybe rename this class?
  AdvertisingDataReader reader(extended_inquiry_response_);

  gap::DataType type;
  common::BufferView data;
  while (reader.GetNextField(&type, &data)) {
    if (type == gap::DataType::kCompleteLocalName) {
      SetName(std::string(data.ToString()));
    }
  }
}

void RemoteDevice::SetInquiryData(const hci::InquiryResult& result) {
  FXL_DCHECK(address_.value() == result.bd_addr);

  bool significant_change =
      !device_class_ ||
      (device_class_->major_class() != result.class_of_device.major_class());
  clock_offset_ = le16toh(kClockOffsetValidBitMask | result.clock_offset);
  page_scan_repetition_mode_ = result.page_scan_repetition_mode;
  device_class_ = result.class_of_device;
  if (significant_change) {
    updated_callback_(this);
  }
}

void RemoteDevice::SetInquiryData(const hci::InquiryResultRSSI& result) {
  FXL_DCHECK(address_.value() == result.bd_addr);

  clock_offset_ = le16toh(kClockOffsetValidBitMask | result.clock_offset);
  page_scan_repetition_mode_ = result.page_scan_repetition_mode;
  device_class_ = result.class_of_device;
  rssi_ = result.rssi;
}

void RemoteDevice::SetInquiryData(
    const hci::ExtendedInquiryResultEventParams& result) {
  FXL_DCHECK(address_.value() == result.bd_addr);

  clock_offset_ = le16toh(kClockOffsetValidBitMask | result.clock_offset);
  page_scan_repetition_mode_ = result.page_scan_repetition_mode;
  device_class_ = result.class_of_device;
  rssi_ = result.rssi;

  SetExtendedInquiryResponse(common::BufferView(
      result.extended_inquiry_response, hci::kExtendedInquiryResponseBytes));
}

void RemoteDevice::SetName(const std::string& name) {
  name_ = name;
  updated_callback_(this);
}

bool RemoteDevice::TryMakeNonTemporary() {
  // TODO(armansito): Since we don't currently support address resolution,
  // random addresses should never be persisted.
  if (!connectable() ||
      address().type() == common::DeviceAddress::Type::kLERandom ||
      address().type() == common::DeviceAddress::Type::kLEAnonymous) {
    FXL_VLOG(1) << "gap: remains temporary: " << ToString();
    return false;
  }

  if (temporary_) {
    temporary_ = false;
    updated_callback_(this);
  }

  return true;
}

std::string RemoteDevice::ToString() const {
  return fxl::StringPrintf("{remote-device id: %s, address: %s}",
                           identifier_.c_str(), address_.ToString().c_str());
}

}  // namespace gap
}  // namespace btlib
