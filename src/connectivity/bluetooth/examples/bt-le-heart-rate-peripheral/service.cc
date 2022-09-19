// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "service.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <iostream>
#include <iterator>
#include <limits>

#include "heart_model.h"

using fuchsia::bluetooth::Status;

namespace gatt = fuchsia::bluetooth::gatt2;

namespace bt_le_heart_rate {
namespace {

// See Heart Rate Service v1.0, 3.1.1.1 Flags Field
constexpr uint8_t kHeartRateValueFormat = 1 << 0;  // 1 for 16 bit rate value
constexpr uint8_t kSensorContactStatus = 1 << 1;
constexpr uint8_t kSensorContactSupported = 1 << 2;
constexpr uint8_t kEnergyExpendedStatus = 1 << 3;
constexpr uint8_t kRrInterval = 1 << 4;

// Convert an integer type to a smaller integer type with saturation.
template <typename To, typename From>
To Narrow(From value) {
  static_assert(sizeof(To) <= sizeof(From), "Can't narrow to a larger type");

  constexpr From lower_limit = std::numeric_limits<To>::min();
  static_assert(lower_limit >= std::numeric_limits<From>::min(), "Not 1:1");
  constexpr From upper_limit = std::numeric_limits<To>::max();
  static_assert(upper_limit <= std::numeric_limits<From>::max(), "Not 1:1");

  value = std::max(value, lower_limit);
  return static_cast<To>(std::min(value, upper_limit));
}

std::vector<uint8_t> MakeMeasurementPayload(int rate, const bool* sensor_contact,
                                            const int* energy_expended, const int* rr_interval) {
  std::vector<uint8_t> payload(1);

  // Compute width of field necessary for the heart rate.
  // Heart Rate Service v1.0, 3.1.1.1.1: "Heart Rate Value Format bit may change
  // during a connection."
  const uint8_t rate_8bit = Narrow<uint8_t>(rate);
  if (rate_8bit == rate) {
    payload.push_back(rate_8bit);
  } else {
    payload[0] |= kHeartRateValueFormat;
    const auto rate_16bit = Narrow<uint16_t>(rate);
    payload.push_back(static_cast<uint8_t>(rate_16bit));
    payload.push_back(static_cast<uint8_t>(rate_16bit >> 8));
  }

  if (sensor_contact) {
    payload[0] |= kSensorContactSupported;
    payload[0] |= *sensor_contact ? kSensorContactStatus : 0;
  }

  // Heart Rate Service v1.0, 3.1.1.3: "If the maximum value of 65535 kilo
  // Joules is attained (0xFFFF), the field value should remain at 0xFFFF."
  if (energy_expended) {
    payload[0] |= kEnergyExpendedStatus;
    const auto energy_expended_16bit = Narrow<uint16_t>(*energy_expended);
    payload.push_back(static_cast<uint8_t>(energy_expended_16bit));
    payload.push_back(static_cast<uint8_t>(energy_expended_16bit >> 8));
  }

  if (rr_interval) {
    payload[0] |= kRrInterval;
    const auto rr_interval_16bit = Narrow<uint16_t>(*rr_interval);
    payload.push_back(static_cast<uint8_t>(rr_interval_16bit));
    payload.push_back(static_cast<uint8_t>(rr_interval_16bit >> 8));
  }

  return payload;
}

void PrintBytes(const std::vector<uint8_t>& value) {
  const auto fmtflags = std::cout.flags();
  std::cout << std::hex;
  std::copy(value.begin(), value.end(), std::ostream_iterator<unsigned>(std::cout));
  std::cout.flags(fmtflags);
}

}  // namespace

Service::Service(std::unique_ptr<HeartModel> heart_model)
    : heart_model_(std::move(heart_model)),
      binding_(this),
      notify_scheduled_(false),
      weak_factory_(this) {
  FX_DCHECK(heart_model_);
}

void Service::PublishService(fuchsia::bluetooth::gatt2::ServerPtr* server) {
  // Heart Rate Measurement
  // Allow update with default security of "none required."
  gatt::Characteristic hrm;
  hrm.set_handle(gatt::Handle{kHeartRateMeasurementId});
  hrm.set_type(kHeartRateMeasurementUuid);
  hrm.set_properties(gatt::CharacteristicPropertyBits::NOTIFY);
  hrm.mutable_permissions()->mutable_update();

  // Body Sensor Location
  gatt::Characteristic bsl;
  bsl.set_handle(gatt::Handle{kBodySensorLocationId});
  bsl.set_type(kBodySensorLocationUuid);
  bsl.set_properties(gatt::CharacteristicPropertyBits::READ);
  bsl.mutable_permissions()->mutable_read();

  // Heart Rate Control Point
  gatt::Characteristic hrcp;
  hrcp.set_handle(gatt::Handle{kHeartRateControlPointId});
  hrcp.set_type(kHeartRateControlPointUuid);
  hrcp.set_properties(gatt::CharacteristicPropertyBits::WRITE);
  hrcp.mutable_permissions()->mutable_write();

  std::vector<gatt::Characteristic> characteristics;
  characteristics.push_back(std::move(hrm));
  characteristics.push_back(std::move(bsl));
  characteristics.push_back(std::move(hrcp));

  gatt::ServiceInfo service_info;
  service_info.set_handle(gatt::ServiceHandle{0});
  service_info.set_kind(gatt::ServiceKind::PRIMARY);
  service_info.set_type(kServiceUuid);
  service_info.set_characteristics(std::move(characteristics));

  std::cout << "Publishing service..." << std::endl;

  fidl::InterfaceHandle<gatt::LocalService> client = binding_.NewBinding();
  ZX_ASSERT(client.is_valid());

  (*server)->PublishService(std::move(service_info), std::move(client), [](auto result) {
    if (result.is_err()) {
      std::cout << "PublishService error: " << static_cast<uint32_t>(result.err()) << std::endl;
      return;
    }
    std::cout << "Heart Rate Service published" << std::endl;
  });
}

void Service::NotifyMeasurement() {
  if (notification_credits_ == 0) {
    return;
  }
  notification_credits_--;

  HeartModel::Measurement measurement = heart_model_->ReadMeasurement();

  const auto payload = MakeMeasurementPayload(measurement.rate, &measurement.contact,
                                              &measurement.energy_expended, nullptr);

  gatt::ValueChangedParameters value;
  value.set_handle(gatt::Handle{kHeartRateMeasurementId});
  value.set_value(std::move(payload));
  binding_.events().OnNotifyValue(std::move(value));
}

void Service::ScheduleNotification() {
  auto self = weak_factory_.GetWeakPtr();
  async::PostDelayedTask(
      async_get_default_dispatcher(),
      [self] {
        if (!self)
          return;

        if (!self->measurement_peers_.empty()) {
          self->NotifyMeasurement();
          self->ScheduleNotification();
        } else {
          self->notify_scheduled_ = false;
        }
      },
      zx::msec(measurement_interval_));

  notify_scheduled_ = true;
}

void Service::CharacteristicConfiguration(fuchsia::bluetooth::PeerId peer_id,
                                          fuchsia::bluetooth::gatt2::Handle handle, bool notify,
                                          bool indicate,
                                          CharacteristicConfigurationCallback callback) {
  std::cout << "CharacteristicConfiguration on peer " << peer_id << " (notify: " << notify
            << ", indicate: " << indicate << ")" << std::endl;

  if (handle.value != kHeartRateMeasurementId) {
    std::cout << "Ignoring configuration for characteristic other than Heart "
                 "Rate Measurement"
              << std::endl;
    return;
  }

  if (notify) {
    std::cout << "Enabling heart rate measurements for peer " << peer_id << std::endl;

    auto insert_res = measurement_peers_.insert(peer_id.value);
    if (insert_res.second && measurement_peers_.size() == 1) {
      if (!notify_scheduled_)
        ScheduleNotification();
    }
  } else {
    std::cout << "Disabling heart rate measurements for peer " << peer_id << std::endl;
    measurement_peers_.erase(peer_id.value);
  }

  callback();
}

void Service::ReadValue(fuchsia::bluetooth::PeerId peer_id,
                        fuchsia::bluetooth::gatt2::Handle handle, int32_t offset,
                        ReadValueCallback callback) {
  std::cout << "ReadValue on characteristic " << handle << " at offset " << offset << std::endl;

  if (handle.value != kBodySensorLocationId) {
    callback(fpromise::error(gatt::Error::READ_NOT_PERMITTED));
    return;
  }

  // Body Sensor Location payload
  std::vector<uint8_t> value;
  value.push_back(static_cast<uint8_t>(BodySensorLocation::kOther));
  callback(fpromise::ok(std::move(value)));
}

void Service::WriteValue(fuchsia::bluetooth::gatt2::LocalServiceWriteValueRequest request,
                         WriteValueCallback callback) {
  std::cout << "WriteValue on characteristic " << request.handle() << " at offset "
            << request.offset() << " (";
  PrintBytes(request.value());
  std::cout << ")" << std::endl;

  if (request.handle().value != kHeartRateControlPointId) {
    std::cout << "Ignoring writes to characteristic other than Heart Rate "
                 "Control Point"
              << std::endl;
    callback(fpromise::error(gatt::Error::WRITE_NOT_PERMITTED));
    return;
  }

  if (request.offset() != 0) {
    std::cout << "Write to control point at invalid offset" << std::endl;
    callback(fpromise::error(gatt::Error::INVALID_OFFSET));
    return;
  }

  if (request.value().size() != 1) {
    std::cout << "Write to control point of invalid length" << std::endl;
    callback(fpromise::error(gatt::Error::INVALID_ATTRIBUTE_VALUE_LENGTH));
    return;
  }

  if (request.value()[0] != kResetEnergyExpendedValue) {
    std::cout << "Write value other than \"Reset Energy Expended\" to "
                 "Heart Rate Control Point characteristic"
              << std::endl;
    callback(fpromise::error(CONTROL_POINT_NOT_SUPPORTED_ERROR));
    return;
  }

  std::cout << "Resetting Energy Expended" << std::endl;
  heart_model_->ResetEnergyExpended();
  callback(fpromise::ok());
}

void Service::ValueChangedCredit(uint8_t additional_credit) {
  notification_credits_ += additional_credit;
}

}  // namespace bt_le_heart_rate
