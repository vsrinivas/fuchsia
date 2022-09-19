// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_EXAMPLES_BT_LE_HEART_RATE_PERIPHERAL_SERVICE_H_
#define SRC_CONNECTIVITY_BLUETOOTH_EXAMPLES_BT_LE_HEART_RATE_PERIPHERAL_SERVICE_H_

#include <fuchsia/bluetooth/gatt2/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fidl/cpp/vector.h>

#include <functional>
#include <unordered_set>

#include "fidl_helpers.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace bt_le_heart_rate {

class HeartModel;

class Service final : fuchsia::bluetooth::gatt2::LocalService {
 public:
  // See assigned numbers for GATT services and characteristics.
  // https://www.bluetooth.com/specifications/assigned-numbers/
  static constexpr fuchsia::bluetooth::Uuid kServiceUuid{{0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00,
                                                          0x80, 0x00, 0x10, 0x00, 0x00, 0x0d, 0x18,
                                                          0x00, 0x00}};
  static constexpr uint64_t kHeartRateMeasurementId = 0;
  static constexpr fuchsia::bluetooth::Uuid kHeartRateMeasurementUuid{
      {0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x37, 0x2a, 0x00,
       0x00}};
  static constexpr uint64_t kBodySensorLocationId = 1;
  static constexpr fuchsia::bluetooth::Uuid kBodySensorLocationUuid{
      {0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x38, 0x2a, 0x00,
       0x00}};
  static constexpr uint64_t kHeartRateControlPointId = 2;
  static constexpr fuchsia::bluetooth::Uuid kHeartRateControlPointUuid{
      {0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x39, 0x2a, 0x00,
       0x00}};

  // From Heart Rate Service v1.0, 1.6.
  static constexpr fuchsia::bluetooth::gatt2::Error CONTROL_POINT_NOT_SUPPORTED_ERROR =
      fuchsia::bluetooth::gatt2::Error::APPLICATION_ERROR_80;

  // Heart Rate Service v1.0, 3.3.1 [Control Point] Characteristic Behavior.
  static constexpr uint8_t kResetEnergyExpendedValue = 0x01;

  explicit Service(std::unique_ptr<HeartModel> heart_model);
  ~Service() = default;

  void PublishService(fuchsia::bluetooth::gatt2::ServerPtr* server);

  void set_measurement_interval(int msec) { measurement_interval_ = msec; }

 private:
  void NotifyMeasurement();

  void ScheduleNotification();

  // LocalService overrides:
  void CharacteristicConfiguration(fuchsia::bluetooth::PeerId peer_id,
                                   fuchsia::bluetooth::gatt2::Handle handle, bool notify,
                                   bool indicate,
                                   CharacteristicConfigurationCallback callback) override;
  void ReadValue(fuchsia::bluetooth::PeerId peer_id, fuchsia::bluetooth::gatt2::Handle handle,
                 int32_t offset, ReadValueCallback callback) override;
  void WriteValue(fuchsia::bluetooth::gatt2::LocalServiceWriteValueRequest request,
                  WriteValueCallback callback) override;
  void ValueChangedCredit(uint8_t additional_credit) override;

  std::unique_ptr<HeartModel> heart_model_;
  fidl::Binding<fuchsia::bluetooth::gatt2::LocalService> binding_;

  // Peers that have enabled measurement notifications.
  std::unordered_set<uint64_t> measurement_peers_;
  bool notify_scheduled_;

  // Interval of measurement notifications in milliseconds.
  int measurement_interval_ = 2000;

  uint32_t notification_credits_ = fuchsia::bluetooth::gatt2::INITIAL_VALUE_CHANGED_CREDITS;

  fxl::WeakPtrFactory<Service> weak_factory_;
};

enum class BodySensorLocation : uint8_t {
  kOther = 0,
  kChest = 1,
  kWrist = 2,
  kFinger = 3,
  kHand = 4,
  kEarLobe = 5,
  kFoot = 6,
};

}  // namespace bt_le_heart_rate

#endif  // SRC_CONNECTIVITY_BLUETOOTH_EXAMPLES_BT_LE_HEART_RATE_PERIPHERAL_SERVICE_H_
