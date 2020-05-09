// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>

#include "src/media/audio/audio_core/testing/fake_audio_driver.h"
#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

const std::string kManufacturer = "Test Manufacturer";
const std::string kProduct = "Test Product";
const audio_stream_unique_id_t kUniqueId = {
    .data = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf},
};
const std::string kUniqueIdString = "000102030405060708090a0b0c0d0e0f";

typedef ::testing::Types<testing::FakeAudioDriverV1, testing::FakeAudioDriverV2> DriverTypes;
TYPED_TEST_SUITE(AudioDeviceServiceTest, DriverTypes);

template <typename T>
class AudioDeviceServiceTest : public HermeticAudioTest {
 protected:
  void SetUp() override;
  void TearDown() override;

  const std::vector<fuchsia::media::AudioDeviceInfo>& devices() { return devices_; }
  fuchsia::media::AudioDeviceEnumerator& audio_device_enumerator() {
    return *audio_device_enumerator_.get();
  }

  uint64_t device_token() { return device_token_; }
  void set_device_token(uint64_t token) { device_token_ = token; }

 private:
  template <typename U>
  void EnumeratorAddDevice(zx::channel remote_channel) {}

  template <>
  void EnumeratorAddDevice<testing::FakeAudioDriverV1>(zx::channel remote_channel) {
    audio_device_enumerator_->AddDeviceByChannel(std::move(remote_channel), "test device", false);
  }

  template <>
  void EnumeratorAddDevice<testing::FakeAudioDriverV2>(zx::channel remote_channel) {
    fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> stream_config = {};
    stream_config.set_channel(std::move(remote_channel));
    audio_device_enumerator_->AddDeviceByChannel2("test device", false, std::move(stream_config));
  }

  fuchsia::media::AudioDeviceEnumeratorPtr audio_device_enumerator_;
  std::vector<fuchsia::media::AudioDeviceInfo> devices_;
  uint64_t device_token_;

  std::unique_ptr<T> driver_;
  fzl::VmoMapper ring_buffer_;
};

template <typename T>
void AudioDeviceServiceTest<T>::SetUp() {
  HermeticAudioTest::SetUp();

  environment()->ConnectToService(audio_device_enumerator_.NewRequest());
  audio_device_enumerator_.set_error_handler(ErrorHandler());

  zx::channel local_channel;
  zx::channel remote_channel;
  zx_status_t status = zx::channel::create(0u, &local_channel, &remote_channel);
  EXPECT_EQ(ZX_OK, status);

  driver_ = std::make_unique<T>(std::move(local_channel), dispatcher());
  ASSERT_NE(driver_, nullptr);
  driver_->set_device_manufacturer(kManufacturer);
  driver_->set_device_product(kProduct);
  driver_->set_stream_unique_id(kUniqueId);
  // Allocate a ring buffer large enough for 1 second of 48khz 2 channel 16-bit audio.
  ring_buffer_ = driver_->CreateRingBuffer(48000 * sizeof(int16_t) * 2);
  driver_->Start();

  audio_device_enumerator_.events().OnDeviceAdded = [this](fuchsia::media::AudioDeviceInfo info) {
    devices_.push_back(std::move(info));
  };

  EnumeratorAddDevice<T>(std::move(remote_channel));
}

template <typename T>
void AudioDeviceServiceTest<T>::TearDown() {
  ASSERT_TRUE(audio_device_enumerator_.is_bound());
  audio_device_enumerator_.events().OnDeviceRemoved = [this](uint64_t dev_token) {
    EXPECT_EQ(dev_token, device_token());
    devices_.clear();
  };

  driver_ = nullptr;
  RunLoopUntil([this]() { return devices().empty(); });

  ASSERT_TRUE(audio_device_enumerator_.is_bound());
  audio_device_enumerator_.Unbind();

  HermeticAudioTest::TearDown();
}

// Test that |AddDeviceByChannel| and |AddDeviceByChannel2| result in an |OnDeviceAdded| event.
TYPED_TEST(AudioDeviceServiceTest, AddDevice) {
  // Expect that the added device is enumerated via the device enumerator.
  this->RunLoopUntil([this]() { return !this->devices().empty(); });

  ASSERT_EQ(1u, this->devices().size());
  auto device = this->devices()[0];
  EXPECT_EQ(kManufacturer + " " + kProduct, device.name);
  EXPECT_EQ(kUniqueIdString, device.unique_id);
  EXPECT_EQ(false, device.is_input);

  this->set_device_token(device.token_id);
}

// Test that the info in |GetDevices| matches the info in the |OnDeviceAdded| event.
TYPED_TEST(AudioDeviceServiceTest, GetDevices) {
  this->RunLoopUntil([this]() { return !this->devices().empty(); });

  std::optional<std::vector<fuchsia::media::AudioDeviceInfo>> devices;
  this->audio_device_enumerator().GetDevices(
      [&devices](std::vector<fuchsia::media::AudioDeviceInfo> devices_in) {
        devices = std::move(devices_in);
      });
  this->RunLoopUntil([&devices]() { return devices.has_value(); });

  ASSERT_EQ(1u, devices->size());
  auto device = (*devices)[0];
  EXPECT_EQ(kManufacturer + " " + kProduct, device.name);
  EXPECT_EQ(kUniqueIdString, device.unique_id);
  EXPECT_EQ(false, device.is_input);

  this->set_device_token(device.token_id);
}

}  // namespace media::audio::test
