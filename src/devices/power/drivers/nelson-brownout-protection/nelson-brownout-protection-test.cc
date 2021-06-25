// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nelson-brownout-protection.h"

#include <fuchsia/hardware/gpio/cpp/banjo-mock.h>
#include <fuchsia/hardware/power/sensor/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/simple-codec/simple-codec-server.h>

#include <optional>

#include <zxtest/zxtest.h>

namespace brownout_protection {

class FakeCodec : public audio::SimpleCodecServer {
 public:
  FakeCodec() : SimpleCodecServer(fake_ddk::kFakeParent) {}
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }

  zx_status_t Shutdown() override { return ZX_OK; }
  zx::status<audio::DriverIds> Initialize() override {
    return zx::ok(audio::DriverIds{.vendor_id = 0, .device_id = 0});
  }
  zx_status_t Reset() override { return ZX_ERR_NOT_SUPPORTED; }
  audio::Info GetInfo() override {
    return {
        .unique_id = "test id",
        .manufacturer = "test man",
        .product_name = "test prod",
    };
  }
  zx_status_t Stop() override { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t Start() override { return ZX_OK; }
  bool IsBridgeable() override { return false; }
  void SetBridgedMode(bool enable_bridged_mode) override {}
  audio::DaiSupportedFormats GetDaiFormats() override { return {}; }
  zx::status<audio::CodecFormatInfo> SetDaiFormat(const audio::DaiFormat& format) override {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  audio::GainFormat GetGainFormat() override { return {.min_gain = -103.0f}; }
  audio::GainState GetGainState() override { return gain_state; }
  void SetGainState(audio::GainState state) override { gain_state = state; }
  inspect::Inspector& inspect() { return SimpleCodecServer::inspect(); }

 private:
  audio::GainState gain_state = {};
};

class FakePowerSensor : public ddk::PowerSensorProtocol<FakePowerSensor, ddk::base_protocol>,
                        public fidl::WireServer<fuchsia_hardware_power_sensor::Device> {
 public:
  explicit FakePowerSensor(async_dispatcher_t* dispatcher)
      : proto_{.ops = &power_sensor_protocol_ops_, .ctx = this}, dispatcher_(dispatcher) {}

  const power_sensor_protocol_t* GetProto() const { return &proto_; }

  zx_status_t PowerSensorConnectServer(zx::channel server) {
    binding_.emplace(fidl::BindServer(
        dispatcher_, fidl::ServerEnd<fuchsia_hardware_power_sensor::Device>(std::move(server)),
        this));
    return ZX_OK;
  }

  void set_voltage(float voltage) { voltage_ = voltage; }

  void GetPowerWatts(GetPowerWattsRequestView request,
                     GetPowerWattsCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void GetVoltageVolts(GetVoltageVoltsRequestView request,
                       GetVoltageVoltsCompleter::Sync& completer) override {
    completer.ReplySuccess(voltage_);
  }

 private:
  float voltage_ = 0.0f;
  const power_sensor_protocol_t proto_;
  async_dispatcher_t* const dispatcher_;
  std::optional<fidl::ServerBindingRef<fuchsia_hardware_power_sensor::Device>> binding_;
};

class Bind : public fake_ddk::Bind {
 public:
  ~Bind() override {
    // Manually delete in case RemoveDevice() wasn't called.
    if (device_) {
      device_->DdkRelease();
      device_ = nullptr;
    }
  }

  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    if (args->proto_id == ZX_PROTOCOL_CODEC) {
      // Ignore the codec which will also call device_add().
      return ZX_OK;
    }

    device_ = reinterpret_cast<NelsonBrownoutProtection*>(args->ctx);
    return fake_ddk::Bind::DeviceAdd(drv, parent, args, out);
  }

  void RemoveDevice() {
    device_async_remove(fake_ddk::kFakeDevice);
    if (device_) {
      device_->DdkRelease();
      device_ = nullptr;
    }
  }

 private:
  NelsonBrownoutProtection* device_ = nullptr;
};

TEST(NelsonBrownoutProtectionTest, Test) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

  std::unique_ptr<FakeCodec> codec = audio::SimpleCodecServer::Create<FakeCodec>();
  codec->SetGainState({10.0f, false, false});
  FakePowerSensor power_sensor(loop.dispatcher());
  ddk::MockGpio alert_gpio;

  fbl::Array<fake_ddk::FragmentEntry> fragments(new fake_ddk::FragmentEntry[3], 3);
  fragments[0].name = "codec";
  fragments[0].protocols.push_back(fake_ddk::ProtocolEntry{
      .id = ZX_PROTOCOL_CODEC,
      .proto =
          {
              .ops = codec->GetProto().ops,
              .ctx = codec->GetProto().ctx,
          },
  });

  fragments[1].name = "power-sensor";
  fragments[1].protocols.push_back(fake_ddk::ProtocolEntry{
      .id = ZX_PROTOCOL_POWER_SENSOR,
      .proto =
          {
              .ops = power_sensor.GetProto()->ops,
              .ctx = power_sensor.GetProto()->ctx,
          },
  });

  fragments[2].name = "alert-gpio";
  fragments[2].protocols.push_back(fake_ddk::ProtocolEntry{
      .id = ZX_PROTOCOL_GPIO,
      .proto =
          {
              .ops = alert_gpio.GetProto()->ops,
              .ctx = alert_gpio.GetProto()->ctx,
          },
  });

  Bind bind;
  bind.SetFragments(std::move(fragments));

  zx::interrupt alert_gpio_interrupt;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &alert_gpio_interrupt));

  {
    zx::interrupt interrupt_dup;
    ASSERT_OK(alert_gpio_interrupt.duplicate(ZX_RIGHT_SAME_RIGHTS, &interrupt_dup));
    alert_gpio.ExpectConfigIn(ZX_OK, GPIO_NO_PULL)
        .ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(interrupt_dup));
  }

  ASSERT_OK(loop.StartThread());
  ASSERT_OK(NelsonBrownoutProtection::Create(nullptr, fake_ddk::kFakeParent));
  EXPECT_FALSE(codec->GetGainState().agc_enabled);

  power_sensor.set_voltage(10.0f);  // Must be less than 11.5 to stay in the brownout state.

  alert_gpio_interrupt.trigger(0, zx::clock::get_monotonic());

  while (!codec->GetGainState().agc_enabled) {
  }

  EXPECT_EQ(codec->GetGainState().gain, 10.0f);
  EXPECT_FALSE(codec->GetGainState().muted);

  power_sensor.set_voltage(12.0f);  // End the brownout state and make sure AGC gets re-enabled.

  while (codec->GetGainState().agc_enabled) {
  }

  EXPECT_EQ(codec->GetGainState().gain, 10.0f);
  EXPECT_FALSE(codec->GetGainState().muted);

  ASSERT_NO_FATAL_FAILURES(alert_gpio.VerifyAndClear());
  bind.RemoveDevice();
  EXPECT_TRUE(bind.Ok());

  codec->DdkRelease();
  __UNUSED auto* _ = codec.release();  // Freed by the previous call.
}

}  // namespace brownout_protection
