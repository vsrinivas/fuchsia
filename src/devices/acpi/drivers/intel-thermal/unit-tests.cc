// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.acpi/cpp/wire_types.h>
#include <fidl/fuchsia.hardware.thermal/cpp/wire_types.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/debug.h>

#include <memory>

#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

#include "src/devices/acpi/drivers/intel-thermal/intel_thermal.h"
#include "src/devices/lib/acpi/mock/mock-acpi.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

#define EPSILON 0.001
#define ASSERT_FLOAT_EQ(float1, float2) \
  ASSERT_LT(fabsf(float1 - float2), EPSILON, "Expected |" #float1 " - " #float2 "| < 0.001.")

namespace intel_thermal {

namespace facpi = fuchsia_hardware_acpi::wire;

constexpr uint8_t kAmbientUtf16[] = {0x41, 0x00, 0x6d, 0x00, 0x62, 0x00, 0x69, 0x00,
                                     0x65, 0x00, 0x6e, 0x00, 0x74, 0x00, 0x00, 0x00};
constexpr const char* kAmbientStr = "Ambient";

constexpr uint64_t kTripPointCount = 10;

using inspect::InspectTestHelper;

class IntelThermalTest : public InspectTestHelper, public zxtest::Test {
 public:
  IntelThermalTest() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  using AcpiDevice = acpi::mock::Device;
  void SetUp() override {
    fake_root_ = MockDevice::FakeRootParent();
    ASSERT_OK(loop_.StartThread("intel-thermal-test-fidl"));
    ASSERT_OK(loop_.StartThread("intel-thermal-test-fidl"));

    fake_acpi_.SetEvaluateObject(
        [this](auto request, auto& completer) { EvaluateObject(request, completer); });
    fake_acpi_.SetInstallNotifyHandler(
        [this](auto request, auto& completer) { InstallNotifyHandler(request, completer); });

    auto client = fake_acpi_.CreateClient(loop_.dispatcher());
    ASSERT_OK(client.status_value());

    auto thermal = std::make_unique<IntelThermal>(fake_root_.get(), std::move(client.value()),
                                                  loop_.dispatcher());
    ASSERT_OK(thermal->Bind());
    IntelThermal* ptr = thermal.release();

    ptr->zxdev()->InitOp();
    ASSERT_OK(ptr->zxdev()->WaitUntilInitReplyCalled(zx::time::infinite()));
    device_ = ptr->zxdev();

    // Start the FIDL server.
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_thermal::Device>();
    ASSERT_OK(endpoints.status_value());

    fidl::BindServer<fidl::WireServer<fuchsia_hardware_thermal::Device>>(
        loop_.dispatcher(), std::move(endpoints->server), ptr);
    thermal_client_.Bind(std::move(endpoints->client));
  }

  void TearDown() override {
    device_async_remove(device_);
    loop_.Shutdown();
    ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(fake_root_.get()));
  }

  void EvaluateObject(AcpiDevice::EvaluateObjectRequestView request,
                      AcpiDevice::EvaluateObjectCompleter::Sync& completer) {
    ASSERT_EQ(request->mode, facpi::EvaluateObjectMode::kPlainObject);
    std::string object(request->path.data(), request->path.size());
    if (object == "_STR") {
      completer.ReplySuccess(facpi::EncodedObject::WithObject(
          arena_, facpi::Object::WithBufferVal(
                      arena_, fidl::VectorView<uint8_t>::FromExternal(
                                  const_cast<uint8_t*>(kAmbientUtf16), sizeof(kAmbientUtf16)))));
    } else if (object == "PTYP") {
      completer.ReplySuccess(facpi::EncodedObject::WithObject(
          arena_, facpi::Object::WithIntegerVal(arena_, kTypeThermalSensor)));
    } else if (object == "PATC") {
      completer.ReplySuccess(facpi::EncodedObject::WithObject(
          arena_, facpi::Object::WithIntegerVal(arena_, kTripPointCount)));
    } else if (object == "_PSV") {
      completer.ReplySuccess(facpi::EncodedObject::WithObject(
          arena_, facpi::Object::WithIntegerVal(arena_, passive_temp_dK_)));
    } else if (object == "_CRT") {
      completer.ReplySuccess(facpi::EncodedObject::WithObject(
          arena_, facpi::Object::WithIntegerVal(arena_, crit_temp_dK_)));
    } else if (object == "_TMP") {
      completer.ReplySuccess(facpi::EncodedObject::WithObject(
          arena_, facpi::Object::WithIntegerVal(arena_, cur_temp_dK_)));
    } else if (object.substr(0, 3) == "PAT" && object.size() == 4) {
      ASSERT_EQ(object[3], '0');
      completer.ReplySuccess(facpi::EncodedObject());
    } else {
      ASSERT_FALSE(true, "Unexpected EvaluateObject: %s", object.data());
    }
  }

  void InstallNotifyHandler(AcpiDevice::InstallNotifyHandlerRequestView request,
                            AcpiDevice::InstallNotifyHandlerCompleter::Sync& completer) {
    ASSERT_EQ(request->mode, facpi::NotificationMode::kDevice);
    ASSERT_FALSE(notify_client_.is_valid());
    notify_client_.Bind(std::move(request->handler));
    completer.ReplySuccess();
  }

 protected:
  std::shared_ptr<zx_device> fake_root_;
  AcpiDevice fake_acpi_;
  async::Loop loop_;
  zx_device* device_;

  // ACPI stores all temperatures in decikelvin.
  uint64_t passive_temp_dK_ = 2932;  // ~20C
  uint64_t crit_temp_dK_ = 3532;     // ~80C
  uint64_t cur_temp_dK_ = 2852;      // ~12C
  std::optional<uint64_t> trip_point_;

  fidl::Arena<> arena_;
  fidl::WireSyncClient<fuchsia_hardware_thermal::Device> thermal_client_;
  fidl::WireSyncClient<fuchsia_hardware_acpi::NotifyHandler> notify_client_;
};

TEST_F(IntelThermalTest, InspectTest) {
  auto* device = device_->GetDeviceContext<IntelThermal>();
  ASSERT_NO_FATAL_FAILURE(ReadInspect(device->inspect_vmo()));
  ASSERT_NO_FATAL_FAILURE(
      CheckProperty(hierarchy().node(), "description", inspect::StringPropertyValue(kAmbientStr)));
}

TEST_F(IntelThermalTest, NotifysOnTrip) {
  auto result = thermal_client_->GetStateChangeEvent();
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  zx::event event = std::move(result->handle);
  zx_signals_t pending;
  ASSERT_STATUS(event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite_past(), &pending),
                ZX_ERR_TIMED_OUT);

  // ACPI notification should set event.
  ASSERT_OK(notify_client_->Handle(kThermalEvent).status());

  ASSERT_OK(event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), &pending));

  // Calling GetInfo should clear event.
  ASSERT_OK(thermal_client_->GetInfo().status());

  ASSERT_STATUS(event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite_past(), &pending),
                ZX_ERR_TIMED_OUT);
}

TEST_F(IntelThermalTest, GetCurrentTemp) {
  auto temp = thermal_client_->GetTemperatureCelsius();
  ASSERT_OK(temp.status());
  ASSERT_OK(temp->status);

  ASSERT_FLOAT_EQ(temp->temp, 12.05f);
}

TEST_F(IntelThermalTest, GetInfo) {
  {
    auto info = thermal_client_->GetInfo();
    ASSERT_OK(info.status());
    ASSERT_OK(info->status);

    ASSERT_FLOAT_EQ(info->info->critical_temp_celsius, 80.05f);
    ASSERT_FLOAT_EQ(info->info->passive_temp_celsius, 20.05f);
    ASSERT_EQ(info->info->max_trip_count, kTripPointCount);
    fidl::Array<float, fuchsia_hardware_thermal::wire::kMaxTripPoints> trip_points{0};
    ASSERT_EQ(memcmp(trip_points.data(), info->info->active_trip.data(),
                     trip_points.size() * sizeof(float)),
              0);
    ASSERT_EQ(info->info->state, fuchsia_hardware_thermal::wire::kThermalStateNormal);
  }

  /* Try setting a trip point and triggering it. */
  thermal_client_->SetTripCelsius(0, 22.05f);
  cur_temp_dK_ = 2972;

  {
    auto info = thermal_client_->GetInfo();
    ASSERT_OK(info.status());
    ASSERT_OK(info->status);

    ASSERT_FLOAT_EQ(info->info->critical_temp_celsius, 80.05f);
    ASSERT_FLOAT_EQ(info->info->passive_temp_celsius, 20.05f);
    ASSERT_EQ(info->info->max_trip_count, kTripPointCount);
    ASSERT_FLOAT_EQ(info->info->active_trip[0], 22.05f);
    ASSERT_EQ(info->info->state, fuchsia_hardware_thermal::wire::kThermalStateTripViolation);
  }
}
}  // namespace intel_thermal
