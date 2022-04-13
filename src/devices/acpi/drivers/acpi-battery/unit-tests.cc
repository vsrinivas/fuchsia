// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

#include "fidl/fuchsia.hardware.acpi/cpp/markers.h"
#include "fidl/fuchsia.hardware.power/cpp/wire_types.h"
#include "lib/async-loop/cpp/loop.h"
#include "lib/fidl/llcpp/connect_service.h"
#include "lib/inspect/cpp/hierarchy.h"
#include "src/devices/acpi/drivers/acpi-battery/acpi_battery.h"
#include "src/devices/lib/acpi/mock/mock-acpi.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace acpi_battery::test {
namespace facpi = fuchsia_hardware_acpi::wire;

constexpr uint32_t kDesignCapacity = 1000;          // mW
constexpr uint32_t kLastFullChargeCapacity = 1000;  // mW
constexpr uint32_t kDesignVoltage = 1000;           // mV
constexpr uint32_t kDesignCapacityWarning = 100;    // mW
constexpr uint32_t kDesignCapacityLow = 50;         // mW
constexpr uint32_t kCapacityGranularity1 = 1;       // mW
constexpr uint32_t kCapacityGranularity2 = 1;       // mW
constexpr uint32_t kCurrentVoltage = 1;             // mV
constexpr const char* kModelNumber = "ACME B123";
constexpr const char* kSerialNumber = "00000001";
constexpr const char* kBatteryType = "Potato";

using inspect::InspectTestHelper;

class AcpiBatteryTest : public InspectTestHelper, public zxtest::Test {
 public:
  AcpiBatteryTest() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  using AcpiDevice = acpi::mock::Device;
  void SetUp() override {
    // Start two threads, because we host both a server and client on this dispatcher.
    ASSERT_OK(loop_.StartThread("acpi-battery-test-fidl"));
    ASSERT_OK(loop_.StartThread("acpi-battery-test-fidl"));
    fake_root_ = MockDevice::FakeRootParent();
    fake_acpi_.SetEvaluateObject(
        [this](auto request, auto& completer) { EvaluateObject(request, completer); });

    fake_acpi_.SetInstallNotifyHandler(
        [this](auto request, auto& completer) { InstallNotifyHandler(request, completer); });

    auto client = fake_acpi_.CreateClient(loop_.dispatcher());
    ASSERT_OK(client.status_value());

    // Run bind() and DdkInit().
    auto battery = std::make_unique<AcpiBattery>(fake_root_.get(), std::move(client.value()),
                                                 loop_.dispatcher());
    ASSERT_OK(battery->Bind());
    AcpiBattery* ptr = battery.release();

    ptr->zxdev()->InitOp();
    ASSERT_OK(ptr->zxdev()->WaitUntilInitReplyCalled(zx::time::infinite()));
    device_ = ptr->zxdev();

    // Start the FIDL server.
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_power::Source>();
    ASSERT_OK(endpoints.status_value());
    fidl::BindServer<fidl::WireServer<fuchsia_hardware_power::Source>>(
        loop_.dispatcher(), std::move(endpoints->server), ptr);
    source_client_.Bind(std::move(endpoints->client));
  }

  void TearDown() override {
    for (auto& device : fake_root_->children()) {
      device_async_remove(device.get());
    }
    loop_.Shutdown();
    ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(fake_root_.get()));
  }

  void InstallNotifyHandler(AcpiDevice::InstallNotifyHandlerRequestView request,
                            AcpiDevice::InstallNotifyHandlerCompleter::Sync& completer) {
    ASSERT_FALSE(notify_client_.is_valid());
    notify_client_.Bind(std::move(request->handler));
    completer.ReplySuccess();
  }

  void EvaluateObject(AcpiDevice::EvaluateObjectRequestView request,
                      AcpiDevice::EvaluateObjectCompleter::Sync& completer) {
    ASSERT_EQ(request->mode, facpi::EvaluateObjectMode::kPlainObject);
    std::string object(request->path.data(), request->path.size());
    std::vector<facpi::Object> objects;
    if (object == "_BIF") {
      objects.resize(BifFields::kBifMax);
      objects[BifFields::kPowerUnit] = MakeObject(int(fpower::BatteryUnit::kMw));
      objects[BifFields::kDesignCapacity] = MakeObject(kDesignCapacity);
      objects[BifFields::kLastFullChargeCapacity] = MakeObject(kLastFullChargeCapacity);
      objects[BifFields::kBatteryTechnology] = MakeObject(uint64_t(0));
      objects[BifFields::kDesignVoltage] = MakeObject(kDesignVoltage);
      objects[BifFields::kDesignCapacityWarning] = MakeObject(kDesignCapacityWarning);
      objects[BifFields::kDesignCapacityLow] = MakeObject(kDesignCapacityLow);
      objects[BifFields::kCapacityGranularity1] = MakeObject(kCapacityGranularity1);
      objects[BifFields::kCapacityGranularity2] = MakeObject(kCapacityGranularity2);
      objects[BifFields::kModelNumber] = MakeObject(kModelNumber);
      objects[BifFields::kSerialNumber] = MakeObject(kSerialNumber);
      objects[BifFields::kBatteryType] = MakeObject(kBatteryType);
      objects[BifFields::kOemInformation] = MakeObject("");
      objects.emplace_back(MakeObject(kDesignVoltage));
    } else if (object == "_BST") {
      objects.resize(BstFields::kBstMax);
      objects[BstFields::kBatteryState] = MakeObject(battery_state_);
      objects[BstFields::kBatteryCurrentRate] = MakeObject(battery_rate_);
      objects[BstFields::kBatteryRemainingCapacity] = MakeObject(battery_capacity_);
      objects[BstFields::kBatteryCurrentVoltage] = MakeObject(kCurrentVoltage);
    } else if (object == "_STA") {
      completer.ReplySuccess(facpi::EncodedObject::WithObject(arena_, MakeObject(0x1f)));
      return;
    }

    auto vv = fidl::VectorView<facpi::Object>::FromExternal(objects);
    facpi::ObjectList list{.value = vv};
    auto retval = facpi::Object::WithPackageVal(arena_, list);
    completer.ReplySuccess(facpi::EncodedObject::WithObject(arena_, retval));
  }

  facpi::Object MakeObject(uint64_t intval) {
    return facpi::Object::WithIntegerVal(arena_, intval);
  }

  facpi::Object MakeObject(const char* strval) {
    return facpi::Object::WithStringVal(arena_, fidl::StringView::FromExternal(strval));
  }

  void CheckInfo() {
    auto res = source_client_->GetBatteryInfo();
    ASSERT_OK(res.status());
    ASSERT_OK(res->status);

    auto& info = res->info;
    ASSERT_EQ(info.unit, fpower::BatteryUnit::kMw);
    ASSERT_EQ(info.design_capacity, kDesignCapacity);
    ASSERT_EQ(info.last_full_capacity, kLastFullChargeCapacity);
    ASSERT_EQ(info.design_voltage, kDesignVoltage);
    ASSERT_EQ(info.capacity_warning, kDesignCapacityWarning);
    ASSERT_EQ(info.capacity_low, kDesignCapacityLow);
    ASSERT_EQ(info.capacity_granularity_low_warning, kCapacityGranularity1);
    ASSERT_EQ(info.capacity_granularity_warning_full, kCapacityGranularity2);
    ASSERT_EQ(info.remaining_capacity, battery_capacity_);
    ASSERT_EQ(info.present_voltage, kCurrentVoltage);

    ASSERT_EQ(info.present_rate,
              battery_state_ & AcpiBatteryState::kDischarging ? -battery_rate_ : battery_rate_);
  }

 protected:
  std::shared_ptr<zx_device> fake_root_;
  AcpiDevice fake_acpi_;
  fidl::Arena<> arena_;
  uint32_t battery_state_ = AcpiBatteryState::kDischarging;
  uint32_t battery_rate_ = 10;  // mWh
  uint32_t battery_capacity_ = kLastFullChargeCapacity;

  async::Loop loop_;
  zx_device* device_;
  fidl::WireSyncClient<fuchsia_hardware_acpi::NotifyHandler> notify_client_;
  fidl::WireSyncClient<fuchsia_hardware_power::Source> source_client_;
};

TEST_F(AcpiBatteryTest, CheckBatteryInfo) { ASSERT_NO_FATAL_FAILURE(CheckInfo()); }

TEST_F(AcpiBatteryTest, CheckSourceInfo) {
  auto res = source_client_->GetPowerInfo();
  ASSERT_OK(res.status());
  ASSERT_OK(res->status);

  auto& info = res->info;
  ASSERT_EQ(info.state, fpower::kPowerStateDischarging | fpower::kPowerStateOnline);
  ASSERT_EQ(info.type, fpower::PowerType::kBattery);
}

TEST_F(AcpiBatteryTest, CheckDataUpdated) {
  auto res = source_client_->GetStateChangeEvent();
  ASSERT_OK(res.status());
  ASSERT_OK(res->status);

  auto event = std::move(res->handle);

  ASSERT_NO_FATAL_FAILURE(CheckInfo());

  zx_signals_t pending = 0;
  ASSERT_STATUS(event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite_past(), &pending),
                ZX_ERR_TIMED_OUT);
  ASSERT_EQ(pending, 0);

  // Percentage point drop!
  battery_capacity_ = 990;
  notify_client_->Handle(0x80);

  // Expect an event.
  ASSERT_OK(event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), &pending));
  ASSERT_EQ(pending, ZX_USER_SIGNAL_0);

  // Check state, which should also clear the event.
  {
    auto res = source_client_->GetPowerInfo();
    ASSERT_OK(res.status());
    ASSERT_OK(res->status);
  }
  ASSERT_STATUS(event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite_past(), &pending),
                ZX_ERR_TIMED_OUT);
  ASSERT_EQ(pending, 0);
}

TEST_F(AcpiBatteryTest, InspectTest) {
  ASSERT_NO_FATAL_FAILURE(ReadInspect(device_->GetDeviceContext<AcpiBattery>()->inspect_vmo()));
  ASSERT_NO_FATAL_FAILURE(CheckProperty(hierarchy().node(), "model-number",
                                        inspect::StringPropertyValue(kModelNumber)));
  ASSERT_NO_FATAL_FAILURE(CheckProperty(hierarchy().node(), "serial-number",
                                        inspect::StringPropertyValue(kSerialNumber)));
  ASSERT_NO_FATAL_FAILURE(CheckProperty(hierarchy().node(), "battery-type",
                                        inspect::StringPropertyValue(kBatteryType)));
}

}  // namespace acpi_battery::test
