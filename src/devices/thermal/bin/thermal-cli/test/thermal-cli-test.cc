// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thermal-cli.h"

#include <fuchsia/hardware/thermal/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-utils/bind.h>
#include <lib/mock-function/mock-function.h>

#include <zxtest/zxtest.h>

class ThermalCliTest : public zxtest::Test {
 public:
  ThermalCliTest() : loop_(&kAsyncLoopConfigAttachToCurrentThread) {
    zx::channel server;
    ASSERT_OK(zx::channel::create(0, &client_, &server));
    fidl_bind(loop_.dispatcher(), server.release(),
              reinterpret_cast<fidl_dispatch_t*>(fuchsia_hardware_thermal_Device_dispatch), this,
              &ops_);
    loop_.StartThread("thermal-cli-test-loop");
  }

  zx::channel GetClient() { return std::move(client_); }

  mock_function::MockFunction<zx_status_t>& MockGetTemperatureCelsius() {
    return mock_GetTemperatureCelsius_;
  }

  mock_function::MockFunction<zx_status_t>& MockGetFanLevel() { return mock_GetFanLevel_; }

  mock_function::MockFunction<zx_status_t, uint32_t>& MockSetFanLevel() {
    return mock_SetFanLevel_;
  }

  mock_function::MockFunction<zx_status_t, fuchsia_hardware_thermal_PowerDomain>&
  MockGetDvfsInfo() {
    return mock_GetDvfsInfo_;
  }

  mock_function::MockFunction<zx_status_t, fuchsia_hardware_thermal_PowerDomain>&
  MockGetDvfsOperatingPoint() {
    return mock_GetDvfsOperatingPoint_;
  }

  mock_function::MockFunction<zx_status_t, uint16_t, fuchsia_hardware_thermal_PowerDomain>&
  MockSetDvfsOperatingPoint() {
    return mock_SetDvfsOperatingPoint_;
  }

 protected:
  using Binder = fidl::Binder<ThermalCliTest>;

  zx_status_t GetInfo(fidl_txn_t* txn) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t GetDeviceInfo(fidl_txn_t* txn) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t GetDvfsInfo(fuchsia_hardware_thermal_PowerDomain power_domain, fidl_txn_t* txn) {
    mock_GetDvfsInfo_.Call(power_domain);

    fuchsia_hardware_thermal_OperatingPointEntry entry0{
        .freq_hz = 100,
        .volt_uv = 42,
    };
    fuchsia_hardware_thermal_OperatingPointEntry entry1{
        .freq_hz = 200,
        .volt_uv = 42,
    };
    fuchsia_hardware_thermal_OperatingPoint op_info{
        .opp = {entry0, entry1},
        .latency = 42,
        .count = 2,
    };
    return fuchsia_hardware_thermal_DeviceGetDvfsInfo_reply(txn, ZX_OK, &op_info);
  }

  zx_status_t GetTemperatureCelsius(fidl_txn_t* txn) {
    zx_status_t status = mock_GetTemperatureCelsius_.Call();
    return fuchsia_hardware_thermal_DeviceGetTemperatureCelsius_reply(txn, status, 0);
  }

  zx_status_t GetStateChangeEvent(fidl_txn_t* txn) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t GetStateChangePort(fidl_txn_t* txn) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t SetTripCelsius(uint32_t id, int32_t temp, fidl_txn_t* txn) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t GetDvfsOperatingPoint(fuchsia_hardware_thermal_PowerDomain power_domain,
                                    fidl_txn_t* txn) {
    mock_GetDvfsOperatingPoint_.Call(power_domain);
    return fuchsia_hardware_thermal_DeviceGetDvfsOperatingPoint_reply(txn, ZX_OK, 1);
  }

  zx_status_t SetDvfsOperatingPoint(uint16_t op_idx,
                                    fuchsia_hardware_thermal_PowerDomain power_domain,
                                    fidl_txn_t* txn) {
    mock_SetDvfsOperatingPoint_.Call(op_idx, power_domain);
    return fuchsia_hardware_thermal_DeviceSetDvfsOperatingPoint_reply(txn, ZX_OK);
  }

  zx_status_t GetFanLevel(fidl_txn_t* txn) {
    zx_status_t status = mock_GetFanLevel_.Call();
    return fuchsia_hardware_thermal_DeviceGetFanLevel_reply(txn, status, 0);
  }

  zx_status_t SetFanLevel(uint32_t fan_level, fidl_txn_t* txn) {
    zx_status_t status = mock_SetFanLevel_.Call(fan_level);
    return fuchsia_hardware_thermal_DeviceSetFanLevel_reply(txn, status);
  }

  async::Loop loop_;
  zx::channel client_;

  mock_function::MockFunction<zx_status_t> mock_GetTemperatureCelsius_;
  mock_function::MockFunction<zx_status_t> mock_GetFanLevel_;
  mock_function::MockFunction<zx_status_t, uint32_t> mock_SetFanLevel_;
  mock_function::MockFunction<zx_status_t, fuchsia_hardware_thermal_PowerDomain> mock_GetDvfsInfo_;
  mock_function::MockFunction<zx_status_t, fuchsia_hardware_thermal_PowerDomain>
      mock_GetDvfsOperatingPoint_;
  mock_function::MockFunction<zx_status_t, uint16_t, fuchsia_hardware_thermal_PowerDomain>
      mock_SetDvfsOperatingPoint_;

  static constexpr fuchsia_hardware_thermal_Device_ops_t ops_ = {
      .GetTemperatureCelsius = Binder::BindMember<&ThermalCliTest::GetTemperatureCelsius>,
      .GetInfo = Binder::BindMember<&ThermalCliTest::GetInfo>,
      .GetDeviceInfo = Binder::BindMember<&ThermalCliTest::GetDeviceInfo>,
      .GetDvfsInfo = Binder::BindMember<&ThermalCliTest::GetDvfsInfo>,
      .GetStateChangeEvent = Binder::BindMember<&ThermalCliTest::GetStateChangeEvent>,
      .GetStateChangePort = Binder::BindMember<&ThermalCliTest::GetStateChangePort>,
      .SetTripCelsius = Binder::BindMember<&ThermalCliTest::SetTripCelsius>,
      .GetDvfsOperatingPoint = Binder::BindMember<&ThermalCliTest::GetDvfsOperatingPoint>,
      .SetDvfsOperatingPoint = Binder::BindMember<&ThermalCliTest::SetDvfsOperatingPoint>,
      .GetFanLevel = Binder::BindMember<&ThermalCliTest::GetFanLevel>,
      .SetFanLevel = Binder::BindMember<&ThermalCliTest::SetFanLevel>,
  };
};

TEST_F(ThermalCliTest, Temperature) {
  ThermalCli thermal_cli(std::move(client_));

  MockGetTemperatureCelsius().ExpectCall(ZX_OK);
  EXPECT_OK(thermal_cli.PrintTemperature());
  MockGetTemperatureCelsius().VerifyAndClear();
}

TEST_F(ThermalCliTest, TemperatureFails) {
  ThermalCli thermal_cli(std::move(client_));

  MockGetTemperatureCelsius().ExpectCall(ZX_ERR_IO);
  EXPECT_EQ(thermal_cli.PrintTemperature(), ZX_ERR_IO);
  MockGetTemperatureCelsius().VerifyAndClear();
}

TEST_F(ThermalCliTest, GetFanLevel) {
  ThermalCli thermal_cli(std::move(client_));

  MockGetFanLevel().ExpectCall(ZX_OK);
  MockSetFanLevel().ExpectNoCall();
  EXPECT_OK(thermal_cli.FanLevelCommand(nullptr));
  MockGetFanLevel().VerifyAndClear();
  MockSetFanLevel().VerifyAndClear();
}

TEST_F(ThermalCliTest, SetFanLevel) {
  ThermalCli thermal_cli(std::move(client_));

  MockGetFanLevel().ExpectNoCall();
  MockSetFanLevel().ExpectCall(ZX_OK, 42);
  EXPECT_OK(thermal_cli.FanLevelCommand("42"));
  MockGetFanLevel().VerifyAndClear();
  MockSetFanLevel().VerifyAndClear();
}

TEST_F(ThermalCliTest, InvalidFanLevel) {
  ThermalCli thermal_cli(std::move(client_));

  MockGetFanLevel().ExpectNoCall();
  MockSetFanLevel().ExpectNoCall();
  EXPECT_EQ(thermal_cli.FanLevelCommand("123abcd"), ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(thermal_cli.FanLevelCommand("-1"), ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(thermal_cli.FanLevelCommand("4294967295"), ZX_ERR_INVALID_ARGS);
  MockGetFanLevel().VerifyAndClear();
  MockSetFanLevel().VerifyAndClear();
}

TEST_F(ThermalCliTest, GetOperatingPoint) {
  ThermalCli thermal_cli(std::move(client_));

  MockGetDvfsInfo().ExpectCall(ZX_OK,
                               fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN);
  MockGetDvfsOperatingPoint().ExpectCall(
      ZX_OK, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN);
  EXPECT_OK(thermal_cli.FrequencyCommand(
      fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, nullptr));
  MockGetDvfsInfo().VerifyAndClear();
  MockGetDvfsOperatingPoint().VerifyAndClear();
}

TEST_F(ThermalCliTest, SetOperatingPoint) {
  ThermalCli thermal_cli(std::move(client_));

  MockGetDvfsInfo().ExpectCall(ZX_OK,
                               fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN);
  MockSetDvfsOperatingPoint().ExpectCall(
      ZX_OK, 1, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN);
  EXPECT_OK(thermal_cli.FrequencyCommand(
      fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, "200"));
  MockGetDvfsInfo().VerifyAndClear();
  MockSetDvfsOperatingPoint().VerifyAndClear();
}

TEST_F(ThermalCliTest, FrequencyNotFound) {
  ThermalCli thermal_cli(std::move(client_));

  MockGetDvfsInfo().ExpectCall(ZX_OK,
                               fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN);
  MockSetDvfsOperatingPoint().ExpectNoCall();
  EXPECT_EQ(thermal_cli.FrequencyCommand(
                fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, "300"),
            ZX_ERR_NOT_FOUND);
  MockGetDvfsInfo().VerifyAndClear();
  MockSetDvfsOperatingPoint().VerifyAndClear();
}

TEST_F(ThermalCliTest, InvalidFrequency) {
  ThermalCli thermal_cli(std::move(client_));

  MockGetDvfsInfo()
      .ExpectCall(ZX_OK, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN)
      .ExpectCall(ZX_OK, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN)
      .ExpectCall(ZX_OK, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN);
  MockSetDvfsOperatingPoint().ExpectNoCall();
  EXPECT_EQ(thermal_cli.FrequencyCommand(
                fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, "123abcd"),
            ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(thermal_cli.FrequencyCommand(
                fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, "-1"),
            ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(thermal_cli.FrequencyCommand(
                fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, "4294967295"),
            ZX_ERR_INVALID_ARGS);
  MockGetDvfsInfo().VerifyAndClear();
  MockSetDvfsOperatingPoint().VerifyAndClear();
}
