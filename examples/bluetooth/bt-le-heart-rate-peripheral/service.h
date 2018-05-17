// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_BLUETOOTH_BT_LE_HEART_RATE_PERIPHERAL_SERVICE_H_
#define GARNET_EXAMPLES_BLUETOOTH_BT_LE_HEART_RATE_PERIPHERAL_SERVICE_H_

#include <unordered_set>

#include <bluetooth_gatt/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fidl/cpp/vector.h>
#include <lib/fxl/memory/weak_ptr.h>

namespace bt_le_heart_rate {

class HeartModel;

class Service final : bluetooth_gatt::LocalServiceDelegate {
 public:
  // See assigned numbers for GATT services and characteristics.
  // https://www.bluetooth.com/specifications/gatt/services
  // https://www.bluetooth.com/specifications/gatt/characteristics
  static constexpr char kServiceUuid[] = "0000180d-0000-1000-8000-00805f9b34fb";
  static constexpr uint64_t kHeartRateMeasurementId = 0;
  static constexpr char kHeartRateMeasurementUuid[] =
      "00002a37-0000-1000-8000-00805f9b34fb";
  static constexpr uint64_t kBodySensorLocationId = 1;
  static constexpr char kBodySensorLocationUuid[] =
      "00002a38-0000-1000-8000-00805f9b34fb";
  static constexpr uint64_t kHeartRateControlPointId = 2;
  static constexpr char kHeartRateControlPointUuid[] =
      "00002a39-0000-1000-8000-00805f9b34fb";

  // See Assigned Numbers for Heart Rate Service
  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.service.heart_rate.xml
  static constexpr bluetooth_gatt::ErrorCode CONTROL_POINT_NOT_SUPPORTED =
      static_cast<bluetooth_gatt::ErrorCode>(0x80);

  // Heart Rate Service v1.0, 3.3.1 [Control Point] Characteristic Behavior.
  static constexpr uint8_t kResetEnergyExpendedValue = 0x01;

  explicit Service(std::unique_ptr<HeartModel> heart_model);
  ~Service() = default;

  void PublishService(bluetooth_gatt::ServerPtr* gatt_server);

  void set_measurement_interval(int msec) { measurement_interval_ = msec; }

 private:
  void NotifyMeasurement();

  void ScheduleNotification();

  // Implement LocalServiceDelegate.
  void OnCharacteristicConfiguration(uint64_t characteristic_id,
                                     fidl::StringPtr peer_id, bool notify,
                                     bool indicate) override;
  void OnReadValue(uint64_t id, int32_t offset,
                   OnReadValueCallback callback) override;
  void OnWriteValue(uint64_t id, uint16_t offset,
                    fidl::VectorPtr<uint8_t> value,
                    OnWriteValueCallback callback) override;
  void OnWriteWithoutResponse(uint64_t id, uint16_t offset,
                              fidl::VectorPtr<uint8_t> value) override;

  std::unique_ptr<HeartModel> heart_model_;
  fidl::Binding<bluetooth_gatt::LocalServiceDelegate> binding_;
  bluetooth_gatt::LocalServicePtr service_;

  // Peers that have enabled measurement notifications.
  std::unordered_set<std::string> measurement_peers_;
  bool notify_scheduled_;

  // Interval of measurement notifications in milliseconds.
  int measurement_interval_ = 2000;

  fxl::WeakPtrFactory<Service> weak_factory_;
};

// Interface for heart rate measurement sensors
class HeartModel {
 public:
  struct Measurement {
    bool contact;         // True if measured while sensor was in contact.
    int rate;             // Heart rate in beats per minute (BPM).
    int energy_expended;  // Energy expended since reset in kilojoules (kJ).
  };

  virtual ~HeartModel() {}

  virtual bool ReadMeasurement(Measurement* measurement) = 0;

  virtual void ResetEnergyExpended() = 0;
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

#endif  // GARNET_EXAMPLES_BLUETOOTH_BT_LE_HEART_RATE_PERIPHERAL_SERVICE_H_
