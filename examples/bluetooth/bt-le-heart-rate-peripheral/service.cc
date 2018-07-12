// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "service.h"

#include <algorithm>
#include <iostream>
#include <iterator>
#include <limits>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fxl/logging.h>

using fuchsia::bluetooth::Status;

namespace gatt = fuchsia::bluetooth::gatt;

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

std::vector<uint8_t> MakeMeasurementPayload(int rate,
                                            const bool* sensor_contact,
                                            const int* energy_expended,
                                            const int* rr_interval) {
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

void PrintBytes(const fidl::VectorPtr<uint8_t>& value) {
  const auto fmtflags = std::cout.flags();
  std::cout << std::hex;
  std::copy(value->begin(), value->end(),
            std::ostream_iterator<unsigned>(std::cout));
  std::cout.flags(fmtflags);
}

}  // namespace

constexpr char Service::kServiceUuid[];
constexpr uint64_t Service::kHeartRateMeasurementId;
constexpr char Service::kHeartRateMeasurementUuid[];
constexpr uint64_t Service::kBodySensorLocationId;
constexpr char Service::kBodySensorLocationUuid[];
constexpr uint64_t Service::kHeartRateControlPointId;
constexpr char Service::kHeartRateControlPointUuid[];

Service::Service(std::unique_ptr<HeartModel> heart_model)
    : heart_model_(std::move(heart_model)),
      binding_(this),
      notify_scheduled_(false),
      weak_factory_(this) {
  FXL_DCHECK(heart_model_);
}

void Service::PublishService(gatt::ServerPtr* gatt_server) {
  // Heart Rate Measurement
  // Allow update with default security of "none required."
  gatt::Characteristic hrm;
  hrm.id = kHeartRateMeasurementId;
  hrm.type = kHeartRateMeasurementUuid;
  hrm.properties = gatt::kPropertyNotify;
  hrm.permissions = gatt::AttributePermissions::New();
  hrm.permissions->update = gatt::SecurityRequirements::New();

  // Body Sensor Location
  gatt::Characteristic bsl;
  bsl.id = kBodySensorLocationId;
  bsl.type = kBodySensorLocationUuid;
  bsl.properties = gatt::kPropertyRead;
  bsl.permissions = gatt::AttributePermissions::New();
  bsl.permissions->read = gatt::SecurityRequirements::New();

  // Heart Rate Control Point
  gatt::Characteristic hrcp;
  hrcp.id = kHeartRateControlPointId;
  hrcp.type = kHeartRateControlPointUuid;
  hrcp.properties = gatt::kPropertyWrite;
  hrcp.permissions = gatt::AttributePermissions::New();
  hrcp.permissions->write = gatt::SecurityRequirements::New();

  fidl::VectorPtr<gatt::Characteristic> characteristics;
  characteristics.push_back(std::move(hrm));
  characteristics.push_back(std::move(bsl));
  characteristics.push_back(std::move(hrcp));

  gatt::ServiceInfo si;
  si.primary = true;
  si.type = kServiceUuid;
  si.characteristics = std::move(characteristics);

  std::cout << "Publishing service..." << std::endl;
  auto publish_svc_result_cb = [](Status status) {
    std::cout << "PublishService status: " << bool(status.error) << std::endl;
  };
  (*gatt_server)
      ->PublishService(std::move(si), binding_.NewBinding(),
                       service_.NewRequest(), std::move(publish_svc_result_cb));
}

void Service::NotifyMeasurement() {
  HeartModel::Measurement measurement = {};
  if (!heart_model_->ReadMeasurement(&measurement))
    return;

  const auto payload =
      MakeMeasurementPayload(measurement.rate, &measurement.contact,
                             &measurement.energy_expended, nullptr);

  for (const auto& peer_id : measurement_peers_) {
    service_->NotifyValue(0, peer_id,
                          fidl::VectorPtr<uint8_t>(std::move(payload)), false);
  }
}

void Service::ScheduleNotification() {
  auto self = weak_factory_.GetWeakPtr();
  async::PostDelayedTask(async_get_default_dispatcher(),
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

void Service::OnCharacteristicConfiguration(uint64_t characteristic_id,
                                            fidl::StringPtr peer_id,
                                            bool notify, bool indicate) {
  std::cout << "CharacteristicConfiguration on peer " << peer_id
            << " (notify: " << notify << ", indicate: " << indicate << ")"
            << std::endl;

  if (characteristic_id != kHeartRateMeasurementId) {
    std::cout << "Ignoring configuration for characteristic other than Heart "
                 "Rate Measurement"
              << std::endl;
    return;
  }

  if (notify) {
    std::cout << "Enabling heart rate measurements for peer " << peer_id
              << std::endl;

    auto insert_res = measurement_peers_.insert(peer_id);
    if (insert_res.second && measurement_peers_.size() == 1) {
      if (!notify_scheduled_)
        ScheduleNotification();
    }
  } else {
    std::cout << "Disabling heart rate measurements for peer " << peer_id
              << std::endl;
    measurement_peers_.erase(peer_id);
  }
}

void Service::OnReadValue(uint64_t id, int32_t offset,
                          OnReadValueCallback callback) {
  std::cout << "ReadValue on characteristic " << id << " at offset " << offset
            << std::endl;

  if (id != kBodySensorLocationId) {
    callback(nullptr, gatt::ErrorCode::NOT_PERMITTED);
    return;
  }

  // Body Sensor Location payload
  fidl::VectorPtr<uint8_t> value;
  value.push_back(static_cast<uint8_t>(BodySensorLocation::kOther));
  callback(std::move(value), gatt::ErrorCode::NO_ERROR);
}

void Service::OnWriteValue(uint64_t id, uint16_t offset,
                           fidl::VectorPtr<uint8_t> value,
                           OnWriteValueCallback callback) {
  std::cout << "WriteValue on characteristic " << id << " at offset " << offset
            << " (";
  PrintBytes(value);
  std::cout << ")" << std::endl;

  if (id != kHeartRateControlPointId) {
    std::cout << "Ignoring writes to characteristic other than Heart Rate "
                 "Control Point"
              << std::endl;
    callback(gatt::ErrorCode::NOT_PERMITTED);
    return;
  }

  if (offset != 0) {
    std::cout << "Write to control point at invalid offset" << std::endl;
    callback(gatt::ErrorCode::INVALID_OFFSET);
    return;
  }

  if (value->size() != 1) {
    std::cout << "Write to control point of invalid length" << std::endl;
    callback(gatt::ErrorCode::INVALID_VALUE_LENGTH);
    return;
  }

  if ((*value)[0] != kResetEnergyExpendedValue) {
    std::cout << "Write value other than \"Reset Energy Expended\" to "
                 "Heart Rate Control Point characteristic"
              << std::endl;
    callback(CONTROL_POINT_NOT_SUPPORTED);
    return;
  }

  std::cout << "Resetting Energy Expended" << std::endl;
  heart_model_->ResetEnergyExpended();
  callback(gatt::ErrorCode::NO_ERROR);
}

void Service::OnWriteWithoutResponse(uint64_t id, uint16_t offset,
                                     fidl::VectorPtr<uint8_t> value) {
  std::cout << "WriteWithoutResponse on characteristic " << id << " at offset "
            << offset << " (";
  PrintBytes(value);
  std::cout << ")" << std::endl;
}

}  // namespace bt_le_heart_rate
