// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pwm.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/metadata.h>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace pwm {

namespace {

constexpr pwm_id_t kTestMetadataIds[] = {{0}};
constexpr size_t kMaxConfigBufferSize = 256;

}  // namespace

struct fake_mode_config {
  uint32_t mode;
};

class FakePwmImpl : public ddk::PwmImplProtocol<FakePwmImpl> {
 public:
  FakePwmImpl()
      : proto_({&pwm_impl_protocol_ops_, this}),
        buffer_(std::make_unique<uint8_t[]>(kMaxConfigBufferSize)) {
    config_.mode_config_buffer = buffer_.get();
    config_.mode_config_size = 0;
  }
  const pwm_impl_protocol_t* proto() const { return &proto_; }

  zx_status_t PwmImplGetConfig(uint32_t idx, pwm_config_t* out_config) {
    get_config_count_++;

    ZX_ASSERT(out_config->mode_config_size >= config_.mode_config_size);

    out_config->polarity = config_.polarity;
    out_config->period_ns = config_.period_ns;
    out_config->duty_cycle = config_.duty_cycle;

    memcpy(out_config->mode_config_buffer, config_.mode_config_buffer, config_.mode_config_size);
    out_config->mode_config_size = config_.mode_config_size;
    return ZX_OK;
  }
  zx_status_t PwmImplSetConfig(uint32_t idx, const pwm_config_t* config) {
    set_config_count_++;

    ZX_ASSERT(config->mode_config_size <= kMaxConfigBufferSize);

    config_.polarity = config->polarity;
    config_.period_ns = config->period_ns;
    config_.duty_cycle = config->duty_cycle;
    memcpy(config_.mode_config_buffer, config->mode_config_buffer, config->mode_config_size);
    config_.mode_config_size = config->mode_config_size;
    return ZX_OK;
  }
  zx_status_t PwmImplEnable(uint32_t idx) {
    enable_count_++;
    return ZX_OK;
  }
  zx_status_t PwmImplDisable(uint32_t idx) {
    disable_count_++;
    return ZX_OK;
  }

  // Accessors
  unsigned int GetConfigCount() const { return get_config_count_; }
  unsigned int SetConfigCount() const { return set_config_count_; }
  unsigned int EnableCount() const { return enable_count_; }
  unsigned int DisableCount() const { return disable_count_; }

 private:
  unsigned int get_config_count_ = 0;
  unsigned int set_config_count_ = 0;
  unsigned int enable_count_ = 0;
  unsigned int disable_count_ = 0;

  pwm_impl_protocol_t proto_;
  pwm_config_t config_;
  std::unique_ptr<uint8_t[]> buffer_;
};

class PwmDeviceTest : public zxtest::Test {
 public:
  PwmDeviceTest() : loop_(&kAsyncLoopConfigAttachToCurrentThread) {}
  void SetUp() override {
    fake_parent_ = MockDevice::FakeRootParent();
    fake_parent_->AddProtocol(ZX_PROTOCOL_PWM_IMPL, fake_pwm_impl_.proto()->ops,
                              fake_pwm_impl_.proto()->ctx);
    fake_parent_->SetMetadata(DEVICE_METADATA_PWM_IDS, &kTestMetadataIds, sizeof(kTestMetadataIds));

    ASSERT_OK(PwmDevice::Create(nullptr, fake_parent_.get()));

    ASSERT_EQ(fake_parent_->child_count(), 1u);

    MockDevice* child_dev = fake_parent_->GetLatestChild();
    pwm_ = child_dev->GetDeviceContext<PwmDevice>();

    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_pwm::Pwm>();
    std::optional<fidl::ServerBindingRef<fuchsia_hardware_pwm::Pwm>> fidl_server;
    fidl_server = fidl::BindServer<fidl::WireServer<fuchsia_hardware_pwm::Pwm>>(
        loop_.dispatcher(), std::move(endpoints->server), pwm_);
    loop_.StartThread("pwm-fidl-test");

    client_ = fidl::BindSyncClient(std::move(endpoints->client));
    ASSERT_TRUE(client_.client_end().is_valid());
  }

  void TearDown() override { loop_.Shutdown(); }

 protected:
  PwmDevice* pwm_;
  fidl::WireSyncClient<fuchsia_hardware_pwm::Pwm> client_;
  std::shared_ptr<MockDevice> fake_parent_;
  FakePwmImpl fake_pwm_impl_;
  async::Loop loop_;
};

TEST_F(PwmDeviceTest, GetConfigTest) {
  pwm_config_t fake_config = {
      false, 0, 0.0, nullptr, 0,
  };
  EXPECT_OK(pwm_->PwmGetConfig(&fake_config));
  EXPECT_OK(pwm_->PwmGetConfig(&fake_config));  // Second time
}

TEST_F(PwmDeviceTest, SetConfigTest) {
  fake_mode_config fake_mode{
      .mode = 0,
  };
  pwm_config_t fake_config{
      .polarity = false,
      .period_ns = 1000,
      .duty_cycle = 45.0,
      .mode_config_buffer = reinterpret_cast<uint8_t*>(&fake_mode),
      .mode_config_size = sizeof(fake_mode),
  };
  EXPECT_OK(pwm_->PwmSetConfig(&fake_config));

  fake_mode.mode = 3;
  fake_config.polarity = true;
  fake_config.duty_cycle = 68.0;
  EXPECT_OK(pwm_->PwmSetConfig(&fake_config));

  EXPECT_OK(pwm_->PwmSetConfig(&fake_config));
}

TEST_F(PwmDeviceTest, EnableTest) {
  EXPECT_OK(pwm_->PwmEnable());
  EXPECT_OK(pwm_->PwmEnable());  // Second time
}

TEST_F(PwmDeviceTest, DisableTest) {
  EXPECT_OK(pwm_->PwmDisable());
  EXPECT_OK(pwm_->PwmDisable());  // Second time
}

TEST_F(PwmDeviceTest, GetConfigFidlTest) {
  // Set a config via the Banjo interface and validate that the same config is
  // returned via the FIDL interface.
  fake_mode_config fake_mode{
      .mode = 0xdeadbeef,
  };
  pwm_config_t fake_config{
      .polarity = false,
      .period_ns = 1000,
      .duty_cycle = 45.0,
      .mode_config_buffer = reinterpret_cast<uint8_t*>(&fake_mode),
      .mode_config_size = sizeof(fake_mode),
  };
  EXPECT_OK(pwm_->PwmSetConfig(&fake_config));

  auto resp = client_.GetConfig();

  ASSERT_TRUE(resp.ok());
  ASSERT_TRUE(resp->result.is_response());
  auto& config = resp->result.response().config;

  EXPECT_EQ(fake_pwm_impl_.EnableCount(), 0);
  EXPECT_EQ(fake_pwm_impl_.DisableCount(), 0);
  EXPECT_EQ(fake_pwm_impl_.GetConfigCount(), 1);
  EXPECT_EQ(fake_pwm_impl_.SetConfigCount(), 1);

  EXPECT_EQ(config.polarity, fake_config.polarity);
  EXPECT_EQ(config.period_ns, fake_config.period_ns);
  EXPECT_EQ(config.duty_cycle, fake_config.duty_cycle);
  EXPECT_EQ(config.mode_config.count(), fake_config.mode_config_size);
  EXPECT_BYTES_EQ(config.mode_config.data(), fake_config.mode_config_buffer,
                  config.mode_config.count());
}

TEST_F(PwmDeviceTest, SetConfigFidlTest) {
  // Set a config via the FIDL interface and validate that the same config is
  // returned via the Banjo interface.
  fake_mode_config fake_mode{
      .mode = 0xdeadbeef,
  };
  fuchsia_hardware_pwm::wire::PwmConfig config;
  config.polarity = true;
  config.period_ns = 1235;
  config.duty_cycle = 45.0;
  config.mode_config = fidl::VectorView<uint8_t>::FromExternal(
      reinterpret_cast<uint8_t*>(&fake_mode), sizeof(fake_mode));

  EXPECT_OK(client_.SetConfig(config));

  pwm_config_t fake_config;
  auto buffer = std::make_unique<uint8_t[]>(kMaxConfigBufferSize);
  fake_config.mode_config_buffer = buffer.get();
  fake_config.mode_config_size = kMaxConfigBufferSize;
  EXPECT_OK(pwm_->PwmGetConfig(&fake_config));

  EXPECT_EQ(fake_pwm_impl_.EnableCount(), 0);
  EXPECT_EQ(fake_pwm_impl_.DisableCount(), 0);
  EXPECT_EQ(fake_pwm_impl_.GetConfigCount(), 1);
  EXPECT_EQ(fake_pwm_impl_.SetConfigCount(), 1);

  EXPECT_EQ(config.polarity, fake_config.polarity);
  EXPECT_EQ(config.period_ns, fake_config.period_ns);
  EXPECT_EQ(config.duty_cycle, fake_config.duty_cycle);
  EXPECT_EQ(config.mode_config.count(), fake_config.mode_config_size);
  EXPECT_BYTES_EQ(config.mode_config.data(), fake_config.mode_config_buffer,
                  config.mode_config.count());
}

TEST_F(PwmDeviceTest, EnableFidlTest) {
  auto enable_resp = client_.Enable();

  ASSERT_OK(enable_resp.status());

  ASSERT_FALSE(enable_resp->result.is_err());

  EXPECT_EQ(fake_pwm_impl_.EnableCount(), 1);
  EXPECT_EQ(fake_pwm_impl_.DisableCount(), 0);
  EXPECT_EQ(fake_pwm_impl_.GetConfigCount(), 0);
  EXPECT_EQ(fake_pwm_impl_.SetConfigCount(), 0);
}

TEST_F(PwmDeviceTest, DisableFidlTest) {
  auto enable_resp = client_.Disable();

  ASSERT_OK(enable_resp.status());

  ASSERT_FALSE(enable_resp->result.is_err());

  EXPECT_EQ(fake_pwm_impl_.EnableCount(), 0);
  EXPECT_EQ(fake_pwm_impl_.DisableCount(), 1);
  EXPECT_EQ(fake_pwm_impl_.GetConfigCount(), 0);
  EXPECT_EQ(fake_pwm_impl_.SetConfigCount(), 0);
}

}  // namespace pwm
