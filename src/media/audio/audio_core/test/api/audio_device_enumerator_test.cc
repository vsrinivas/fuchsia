// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>

#include "src/media/audio/audio_core/testing/fake_audio_driver.h"
#include "src/media/audio/lib/test/hermetic_audio_test.h"

using ASF = fuchsia::media::AudioSampleFormat;
using AudioDeviceInfo = fuchsia::media::AudioDeviceInfo;
using AudioGainInfo = fuchsia::media::AudioGainInfo;
using AudioGainInfoFlags = fuchsia::media::AudioGainInfoFlags;
using AudioGainValidFlags = fuchsia::media::AudioGainValidFlags;

namespace media::audio::test {

namespace {
constexpr size_t kFrameRate = 48000;
static const auto kFormat = Format::Create<ASF::SIGNED_16>(1, kFrameRate).value();
}  // namespace

// This test directly changes events in audio_dev_enum_. These changes are safe because
// we never change events before calling CreateInput or CreateOutput, and we use Unbind
// when we remove a device manually.
class AudioDeviceEnumeratorTest : public HermeticAudioTest {
 protected:
  template <typename DeviceT>
  void TestSetDeviceGain(DeviceT* device) {
    device->fidl().events().OnSetGain = nullptr;
    audio_dev_enum_.events().OnDeviceGainChanged =
        AddCallback("OnDeviceGainChanged", [device](uint64_t token, AudioGainInfo info) {
          EXPECT_EQ(token, device->token());
          EXPECT_EQ(info.gain_db, -30);
          EXPECT_EQ(info.flags, AudioGainInfoFlags(0));
        });
    audio_dev_enum_->SetDeviceGain(
        device->token(), AudioGainInfo{.gain_db = -30.0},
        AudioGainValidFlags::GAIN_VALID | AudioGainValidFlags::MUTE_VALID);
    ExpectCallback();
  }

  template <typename DeviceT>
  void TestDeviceInitializesToUnityGain(DeviceT* device) {
    audio_dev_enum_->GetDeviceGain(
        device->token(), AddCallback("GetDeviceGain", [device](uint64_t token, AudioGainInfo info) {
          EXPECT_EQ(token, device->token());
          EXPECT_FLOAT_EQ(info.gain_db, 0);
        }));
    ExpectCallback();
  }

  template <typename Interface>
  struct DeviceHolder {
    fidl::InterfacePtr<Interface> ptr;
    uint64_t token;
  };

  template <typename CreateDeviceT>
  void TestPlugUnplugDurability(CreateDeviceT create_device) {
    DevicePlugProperties plug_properties = {
        .plugged = true,
        .hardwired = false,
        .can_notify = true,
    };

    // Create two unique devices.
    auto d1 = (*this.*create_device)({0x01, 0x00}, kFormat, kFrameRate, plug_properties, 0);
    auto d2 = (*this.*create_device)({0x02, 0x00}, kFormat, kFrameRate, plug_properties, 0);

    // Take control of these events.
    audio_dev_enum_.events().OnDeviceAdded = nullptr;
    audio_dev_enum_.events().OnDeviceRemoved = nullptr;
    audio_dev_enum_.events().OnDefaultDeviceChanged = nullptr;

    // Repeat the plug-unplug cycle many times.
    for (int k = 0; k < 20; k++) {
      // Unplug d2.
      d2->fidl()->ChangePlugState(zx::clock::get_monotonic().get(), false);
      audio_dev_enum_.events().OnDefaultDeviceChanged = AddCallback(
          "OnDefaultDeviceChanged after unplug Device2",
          [d1](uint64_t old_token, uint64_t new_token) { EXPECT_EQ(new_token, d1->token()); });
      ExpectCallback();

      // Unplug d1.
      d1->fidl()->ChangePlugState(zx::clock::get_monotonic().get(), false);
      audio_dev_enum_.events().OnDefaultDeviceChanged =
          AddCallback("OnDefaultDeviceChanged after unplug Device1",
                      [](uint64_t old_token, uint64_t new_token) { EXPECT_EQ(new_token, 0u); });
      ExpectCallback();

      // Plug d1.
      d1->fidl()->ChangePlugState(zx::clock::get_monotonic().get(), true);
      audio_dev_enum_.events().OnDefaultDeviceChanged = AddCallback(
          "OnDefaultDeviceChanged after plug Device1",
          [d1](uint64_t old_token, uint64_t new_token) { EXPECT_EQ(new_token, d1->token()); });
      ExpectCallback();

      // Plug d2.
      d2->fidl()->ChangePlugState(zx::clock::get_monotonic().get(), true);
      audio_dev_enum_.events().OnDefaultDeviceChanged = AddCallback(
          "OnDefaultDeviceChanged after plug Device2",
          [d2](uint64_t old_token, uint64_t new_token) { EXPECT_EQ(new_token, d2->token()); });
      ExpectCallback();
    }

    Unbind(d1);
    Unbind(d2);
  }

  template <typename CreateDeviceT>
  void TestAddRemoveMany(CreateDeviceT create_device) {
    std::vector<uint64_t> known_tokens;
    // Too many iterations has a tendancy to time out on CQ.
    for (uint8_t k = 0; k < 25; k++) {
      auto d = (*this.*create_device)({k, 0x00}, kFormat, kFrameRate, std::nullopt, 0);
      known_tokens.push_back(d->token());

      // Test GetDevices().
      std::vector<uint64_t> got_tokens;
      audio_dev_enum_->GetDevices(
          AddCallback("GetDevices", [&got_tokens](std::vector<AudioDeviceInfo> devices) {
            for (auto& d : devices) {
              got_tokens.push_back(d.token_id);
            }
          }));
      ExpectCallback();
      EXPECT_THAT(got_tokens, ::testing::UnorderedElementsAreArray(known_tokens));
    }

    // TearDown will test device removal.
  }
};

TEST_F(AudioDeviceEnumeratorTest, OnDeviceGainChangedIgnoresInvalidTokensInSets) {
  // Neither of these commands should trigger an event.
  audio_dev_enum_->SetDeviceGain(ZX_KOID_INVALID, AudioGainInfo{.gain_db = -30.0},
                                 AudioGainValidFlags::GAIN_VALID);
  audio_dev_enum_->SetDeviceGain(33, AudioGainInfo{.gain_db = -30.0},
                                 AudioGainValidFlags::GAIN_VALID);

  audio_dev_enum_.events().OnDeviceGainChanged = AddUnexpectedCallback("OnDeviceGainChanged");

  // Since this call happens after the above calls, any event triggered by
  // the above calls should have been received by the time this call returns.
  audio_dev_enum_->GetDevices(AddCallback("GetDevices"));
  ExpectCallback();
}

TEST_F(AudioDeviceEnumeratorTest, SetDeviceGain_Input) {
  TestSetDeviceGain(CreateInput({0xff, 0x00}, kFormat, kFrameRate));
}

TEST_F(AudioDeviceEnumeratorTest, SetDeviceGain_Output) {
  TestSetDeviceGain(CreateOutput({0xff, 0x00}, kFormat, kFrameRate));
}

TEST_F(AudioDeviceEnumeratorTest, DeviceInitializesToUnityGain_Input) {
  TestDeviceInitializesToUnityGain(CreateInput({0xff, 0x00}, kFormat, kFrameRate));
}

TEST_F(AudioDeviceEnumeratorTest, DeviceInitializesToUnityGain_Output) {
  TestDeviceInitializesToUnityGain(CreateOutput({0xff, 0x00}, kFormat, kFrameRate));
}

TEST_F(AudioDeviceEnumeratorTest, AddRemoveDevice_Input) {
  // Internally, this exercises OnDeviceAdded, and TearDown exercises OnDeviceRemoved,
  // and both exercise OnDefaultDeviceChanged.
  CreateInput({0xff, 0x00}, kFormat, kFrameRate);
}

TEST_F(AudioDeviceEnumeratorTest, AddRemoveDevice_Output) {
  // Internally, this exercises OnDeviceAdded, and TearDown exercises OnDeviceRemoved.
  // and both exercise OnDefaultDeviceChanged.
  CreateOutput({0xff, 0x00}, kFormat, kFrameRate);
}

TEST_F(AudioDeviceEnumeratorTest, RemoveDeviceUnplugged_Input) {
  auto device = CreateInput({0xff, 0x00}, kFormat, kFrameRate);
  device->fidl()->ChangePlugState(zx::clock::get_monotonic().get(), false);
  RunLoopUntilIdle();
}

TEST_F(AudioDeviceEnumeratorTest, RemoveDeviceUnplugged_Output) {
  auto device = CreateOutput({0xff, 0x00}, kFormat, kFrameRate);
  device->fidl()->ChangePlugState(zx::clock::get_monotonic().get(), false);
  RunLoopUntilIdle();
}

TEST_F(AudioDeviceEnumeratorTest, PlugUnplugDurability_Input) {
  // In the following expression, C++ insists that we explicitly name the current class,
  // i.e. typeof(this). This name is created by the TEST_F macro.
  TestPlugUnplugDurability(
      &AudioDeviceEnumeratorTest_PlugUnplugDurability_Input_Test::CreateInput<ASF::SIGNED_16>);
}

TEST_F(AudioDeviceEnumeratorTest, PlugUnplugDurability_Output) {
  // In the following expression, C++ insists that we explicitly name the current class,
  // i.e. typeof(this). This name is created by the TEST_F macro.
  TestPlugUnplugDurability(
      &AudioDeviceEnumeratorTest_PlugUnplugDurability_Output_Test::CreateOutput<ASF::SIGNED_16>);
}

TEST_F(AudioDeviceEnumeratorTest, AddRemoveMany_Input) {
  // In the following expression, C++ insists that we explicitly name the current class,
  // i.e. typeof(this). This name is created by the TEST_F macro.
  TestAddRemoveMany(
      &AudioDeviceEnumeratorTest_AddRemoveMany_Input_Test::CreateInput<ASF::SIGNED_16>);
}

TEST_F(AudioDeviceEnumeratorTest, AddRemoveMany_Output) {
  // In the following expression, C++ insists that we explicitly name the current class,
  // i.e. typeof(this). This name is created by the TEST_F macro.
  TestAddRemoveMany(
      &AudioDeviceEnumeratorTest_AddRemoveMany_Output_Test::CreateOutput<ASF::SIGNED_16>);
}

// The following tests use AddDeviceByChannel to add devices, rather than using
// CreateInput or CreateOutput.
namespace {
static const std::string kManufacturer = "Test Manufacturer";
static const std::string kProduct = "Test Product";
static const audio_stream_unique_id_t kUniqueId = {
    .data = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf},
};
static const std::string kUniqueIdString = "000102030405060708090a0b0c0d0e0f";
}  // namespace

typedef ::testing::Types<testing::FakeAudioDriverV1, testing::FakeAudioDriverV2> DriverTypes;
TYPED_TEST_SUITE(AudioDeviceEnumeratorAddByChannelTest, DriverTypes);

template <typename T>
class AudioDeviceEnumeratorAddByChannelTest : public HermeticAudioTest {
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
void AudioDeviceEnumeratorAddByChannelTest<T>::SetUp() {
  HermeticAudioTest::SetUp();
  audio_device_enumerator_ = TakeOwnershipOfAudioDeviceEnumerator();

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
void AudioDeviceEnumeratorAddByChannelTest<T>::TearDown() {
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
TYPED_TEST(AudioDeviceEnumeratorAddByChannelTest, AddDevice) {
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
TYPED_TEST(AudioDeviceEnumeratorAddByChannelTest, GetDevices) {
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
