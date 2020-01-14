// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_driver.h"

#include "src/media/audio/audio_core/testing/fake_audio_device.h"
#include "src/media/audio/audio_core/testing/fake_audio_driver.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"

namespace media::audio {
namespace {

class AudioDriverTest : public testing::ThreadingModelFixture {
 public:
  void SetUp() override {
    zx::channel c1, c2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &c1, &c2));
    remote_driver_ = std::make_unique<testing::FakeAudioDriver>(std::move(c1), dispatcher());
    ASSERT_EQ(ZX_OK, driver_.Init(std::move(c2)));
  }

 protected:
  testing::StubDeviceRegistry device_registry_;
  std::shared_ptr<testing::FakeAudioOutput> device_{
      testing::FakeAudioOutput::Create(&threading_model(), &device_registry_)};
  AudioDriver driver_{device_.get(), [this](auto delay) { last_late_command_ = delay; }};
  // While |driver_| is the object under test, this object simulates the channel messages that
  // normally come from the actual driver instance.
  std::unique_ptr<testing::FakeAudioDriver> remote_driver_;
  zx::duration last_late_command_ = zx::duration::infinite();
};

TEST_F(AudioDriverTest, GetDriverInfo) {
  remote_driver_->Start();

  driver_.GetDriverInfo();
  RunLoopUntilIdle();
  EXPECT_TRUE(device_->driver_info_fetched());
  EXPECT_EQ(driver_.state(), AudioDriver::State::Unconfigured);
}

TEST_F(AudioDriverTest, GetDriverInfoTimeout) {
  remote_driver_->Stop();

  driver_.GetDriverInfo();

  // DriverInfo still pending.
  RunLoopFor(AudioDriver::kDefaultShortCmdTimeout - zx::nsec(1));
  EXPECT_FALSE(device_->driver_info_fetched());
  EXPECT_EQ(driver_.state(), AudioDriver::State::MissingDriverInfo);

  // Now time out (run 10ms past the deadline).
  RunLoopFor(zx::msec(10) + zx::nsec(1));
  EXPECT_FALSE(device_->driver_info_fetched());
  EXPECT_EQ(driver_.state(), AudioDriver::State::MissingDriverInfo);
  EXPECT_EQ(last_late_command_, zx::duration::infinite());

  // Now run the driver to process the response.
  remote_driver_->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(last_late_command_, zx::msec(10));
  EXPECT_TRUE(device_->driver_info_fetched());
  EXPECT_EQ(driver_.state(), AudioDriver::State::Unconfigured);
}

}  // namespace
}  // namespace media::audio
