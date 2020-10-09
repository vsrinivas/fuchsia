// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/hermetic_audio_test.h"

#include <lib/inspect/cpp/hierarchy.h>

#include <sstream>
#include <vector>

#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/media/audio/audio_core/audio_device.h"
#include "src/media/audio/lib/format/format.h"
#include "src/media/audio/lib/logging/logging.h"
#include "src/media/audio/lib/test/capturer_shim.h"
#include "src/media/audio/lib/test/hermetic_audio_environment.h"
#include "src/media/audio/lib/test/inspect.h"
#include "src/media/audio/lib/test/renderer_shim.h"
#include "src/media/audio/lib/test/test_fixture.h"
#include "src/media/audio/lib/test/virtual_device.h"

namespace media::audio::test {

std::optional<HermeticAudioEnvironment::Options> HermeticAudioTest::test_suite_options_;

void HermeticAudioTest::SetTestSuiteEnvironmentOptions(HermeticAudioEnvironment::Options options) {
  test_suite_options_ = options;
}

void HermeticAudioTest::SetUpEnvironment() {
  auto options = test_suite_options_.value_or(HermeticAudioEnvironment::Options());
  environment_ = std::make_unique<HermeticAudioEnvironment>(options);
  environment_->ConnectToService(virtual_audio_control_sync_.NewRequest());
  virtual_audio_control_sync_->Enable();
}

void HermeticAudioTest::TearDownEnvironment() {
  if (virtual_audio_control_sync_.is_bound()) {
    virtual_audio_control_sync_->Disable();
  }
  environment_ = nullptr;
}

void HermeticAudioTest::SetUp() {
  SetUpEnvironment();
  TestFixture::SetUp();

  environment_->ConnectToService(audio_core_.NewRequest());
  AddErrorHandler(audio_core_, "AudioCore");

  environment_->ConnectToService(ultrasound_factory_.NewRequest());
  AddErrorHandler(ultrasound_factory_, "UltrasoundFactory");

  environment_->ConnectToService(audio_dev_enum_.NewRequest());
  AddErrorHandler(audio_dev_enum_, "AudioDeviceEnumerator");
  WatchForDeviceArrivals();

  // A race can occur in which a device is added before the OnDeviceAdded callback is registered,
  // which causes the OnDefaultDeviceChanged callback to fail to recognize the default device.
  //
  // Here, any devices missed by OnDeviceAdded are accounted for; OnDefaultDeviceChanged processes
  // the most recent pending_default_device_token_ once initial_devices_received_.
  audio_dev_enum_->GetDevices([this](std::vector<fuchsia::media::AudioDeviceInfo> devices) {
    for (const auto& info : devices) {
      if (token_to_unique_id_.count(info.token_id) == 0) {
        OnDeviceAdded(info);
      }
    }
    initial_devices_received_ = true;
    while (!pending_default_device_tokens_.empty()) {
      OnDefaultDeviceChanged(0, pending_default_device_tokens_.front());
      pending_default_device_tokens_.pop();
    }
  });
}

void HermeticAudioTest::TearDown() {
  // Remove all components.
  for (auto& [_, device] : devices_) {
    device.output = nullptr;
    device.input = nullptr;
  }
  capturers_.clear();
  renderers_.clear();

  if (audio_dev_enum_.is_bound()) {
    WaitForDeviceDepartures();
  }

  TestFixture::TearDown();
  TearDownEnvironment();
}

template <fuchsia::media::AudioSampleFormat SampleFormat>
VirtualOutput<SampleFormat>* HermeticAudioTest::CreateOutput(
    const audio_stream_unique_id_t& device_id, TypedFormat<SampleFormat> format, size_t frame_count,
    std::optional<DevicePlugProperties> plug_properties, float device_gain_db) {
  FX_CHECK(SampleFormat != fuchsia::media::AudioSampleFormat::UNSIGNED_8)
      << "hardware is not expected to support UNSIGNED_8";
  FX_CHECK(audio_dev_enum_.is_bound());

  auto ptr = std::make_unique<VirtualOutput<SampleFormat>>(
      static_cast<TestFixture*>(this), environment_.get(), device_id, format, frame_count,
      virtual_output_next_inspect_id_++, plug_properties, device_gain_db);
  auto out = ptr.get();
  auto id = AudioDevice::UniqueIdToString(device_id);
  devices_[id].output = std::move(ptr);

  // Wait until the device is connected.
  RunLoopUntil([this, id, out]() { return out->Ready() && devices_[id].info != std::nullopt; });

  // Wait for device to become the default.
  RunLoopUntil([this, id]() { return devices_[id].is_default; });
  ExpectNoUnexpectedErrors("during CreateOutput");
  return out;
}

template <fuchsia::media::AudioSampleFormat SampleFormat>
VirtualInput<SampleFormat>* HermeticAudioTest::CreateInput(
    const audio_stream_unique_id_t& device_id, TypedFormat<SampleFormat> format, size_t frame_count,
    std::optional<DevicePlugProperties> plug_properties, float device_gain_db) {
  FX_CHECK(SampleFormat != fuchsia::media::AudioSampleFormat::UNSIGNED_8)
      << "hardware is not expected to support UNSIGNED_8";
  FX_CHECK(audio_dev_enum_.is_bound());

  auto ptr = std::make_unique<VirtualInput<SampleFormat>>(
      static_cast<TestFixture*>(this), environment_.get(), device_id, format, frame_count,
      virtual_input_next_inspect_id_++, plug_properties, device_gain_db);
  auto out = ptr.get();
  auto id = AudioDevice::UniqueIdToString(device_id);
  devices_[id].input = std::move(ptr);

  // Wait until the device is connected.
  RunLoopUntil([this, out, id]() { return out->Ready() && devices_[id].info != std::nullopt; });
  return out;
}

template <fuchsia::media::AudioSampleFormat SampleFormat>
AudioRendererShim<SampleFormat>* HermeticAudioTest::CreateAudioRenderer(
    TypedFormat<SampleFormat> format, size_t frame_count, fuchsia::media::AudioRenderUsage usage,
    std::optional<zx::clock> reference_clock) {
  auto ptr = std::make_unique<AudioRendererShim<SampleFormat>>(
      static_cast<TestFixture*>(this), audio_core_, format, frame_count, usage,
      renderer_shim_next_inspect_id_++, std::move(reference_clock));
  auto out = ptr.get();
  renderers_.push_back(std::move(ptr));

  // Wait until the renderer is connected.
  RunLoopUntil([this, out]() { return ErrorOccurred() || out->created(); });
  return out;
}

template <fuchsia::media::AudioSampleFormat SampleFormat>
AudioCapturerShim<SampleFormat>* HermeticAudioTest::CreateAudioCapturer(
    TypedFormat<SampleFormat> format, size_t frame_count,
    fuchsia::media::AudioCapturerConfiguration config) {
  auto ptr = std::make_unique<AudioCapturerShim<SampleFormat>>(
      static_cast<TestFixture*>(this), audio_core_, format, frame_count, std::move(config),
      capturer_shim_next_inspect_id_++);
  auto out = ptr.get();
  capturers_.push_back(std::move(ptr));
  return out;
}

template <fuchsia::media::AudioSampleFormat SampleFormat>
UltrasoundRendererShim<SampleFormat>* HermeticAudioTest::CreateUltrasoundRenderer(
    TypedFormat<SampleFormat> format, size_t frame_count, bool wait_for_creation) {
  auto ptr = std::make_unique<UltrasoundRendererShim<SampleFormat>>(
      static_cast<TestFixture*>(this), ultrasound_factory_, format, frame_count,
      renderer_shim_next_inspect_id_++);
  auto out = ptr.get();
  renderers_.push_back(std::move(ptr));

  if (wait_for_creation) {
    out->WaitForDevice();
  }
  return out;
}

template <fuchsia::media::AudioSampleFormat SampleFormat>
UltrasoundCapturerShim<SampleFormat>* HermeticAudioTest::CreateUltrasoundCapturer(
    TypedFormat<SampleFormat> format, size_t frame_count, bool wait_for_creation) {
  auto ptr = std::make_unique<UltrasoundCapturerShim<SampleFormat>>(
      static_cast<TestFixture*>(this), ultrasound_factory_, format, frame_count,
      capturer_shim_next_inspect_id_++);
  auto out = ptr.get();
  capturers_.push_back(std::move(ptr));
  if (wait_for_creation) {
    out->WaitForDevice();
  }
  return out;
}

void HermeticAudioTest::Unbind(VirtualInputImpl* device) {
  auto it = std::find_if(devices_.begin(), devices_.end(),
                         [device](const auto& it) { return it.second.input.get() == device; });
  FX_CHECK(it != devices_.end());

  device->fidl().Unbind();
  devices_.erase(it);
}

void HermeticAudioTest::Unbind(VirtualOutputImpl* device) {
  auto it = std::find_if(devices_.begin(), devices_.end(),
                         [device](const auto& it) { return it.second.output.get() == device; });
  FX_CHECK(it != devices_.end());

  device->fidl().Unbind();
  devices_.erase(it);
}

void HermeticAudioTest::Unbind(RendererShimImpl* renderer) {
  auto it = std::find_if(
      renderers_.begin(), renderers_.end(),
      [renderer](const std::unique_ptr<RendererShimImpl>& p) { return p.get() == renderer; });
  FX_CHECK(it != renderers_.end());

  renderer->fidl().Unbind();
  renderers_.erase(it);
}

void HermeticAudioTest::Unbind(CapturerShimImpl* capturer) {
  auto it = std::find_if(
      capturers_.begin(), capturers_.end(),
      [capturer](const std::unique_ptr<CapturerShimImpl>& p) { return p.get() == capturer; });
  FX_CHECK(it != capturers_.end());

  capturer->fidl().Unbind();
  capturers_.erase(it);
}

void HermeticAudioTest::WatchForDeviceArrivals() {
  audio_dev_enum_.events().OnDeviceAdded = [this](fuchsia::media::AudioDeviceInfo info) {
    if (token_to_unique_id_.count(info.token_id) > 0) {
      FAIL() << "Device with token " << info.token_id << " already exists";
    }
    OnDeviceAdded(info);
  };

  audio_dev_enum_.events().OnDeviceRemoved = [this](uint64_t token) {
    if (token_to_unique_id_.count(token) == 0) {
      FAIL() << "Unknown device with token " << token;
    }
    auto id = token_to_unique_id_[token];
    ADD_FAILURE() << "Unexpected removal of device " << id;
  };

  audio_dev_enum_.events().OnDeviceGainChanged = [this](uint64_t token,
                                                        fuchsia::media::AudioGainInfo gain_info) {
    if (token_to_unique_id_.count(token) == 0) {
      FAIL() << "Unknown device with token " << token;
    }
    auto id = token_to_unique_id_[token];
    if (devices_[id].info == std::nullopt) {
      FAIL() << "Device has not been added " << id;
    }
    devices_[id].info->gain_info = gain_info;
    AUDIO_LOG(DEBUG) << "Our output device (" << id << ") changed gain: " << gain_info.gain_db
                     << " dB, "
                     << (((gain_info.flags & fuchsia::media::AudioGainInfoFlags::MUTE) ==
                          fuchsia::media::AudioGainInfoFlags::MUTE)
                             ? "MUTE"
                             : "UNMUTE");
  };

  audio_dev_enum_.events().OnDefaultDeviceChanged = [this](uint64_t old_default_token,
                                                           uint64_t new_default_token) {
    OnDefaultDeviceChanged(old_default_token, new_default_token);
    AUDIO_LOG(DEBUG) << "Default device changed (old_token = " << old_default_token
                     << ", new_token = " << new_default_token << ")";
  };
}

void HermeticAudioTest::WaitForDeviceDepartures() {
  audio_dev_enum_.events().OnDeviceAdded = [](fuchsia::media::AudioDeviceInfo device) {
    ADD_FAILURE() << "Unexpected device " << device.unique_id << " added during shutdown";
  };

  audio_dev_enum_.events().OnDeviceRemoved = [this](uint64_t token) {
    if (token_to_unique_id_.count(token) == 0) {
      FAIL() << "Unknown device with token " << token;
    }
    auto id = token_to_unique_id_[token];
    EXPECT_FALSE(devices_[id].is_removed) << "Duplicate removal of device " << id << " in shutdown";
    EXPECT_FALSE(devices_[id].is_default) << "Device was removed while it was still the default!";
    devices_[id].is_removed = true;
  };

  audio_dev_enum_.events().OnDeviceGainChanged = [](uint64_t device_token,
                                                    fuchsia::media::AudioGainInfo) {
    ADD_FAILURE() << "Unexpected device gain changed (" << device_token << ") during shutdown";
  };

  audio_dev_enum_.events().OnDefaultDeviceChanged = [this](uint64_t old_default_token,
                                                           uint64_t new_default_token) {
    OnDefaultDeviceChanged(old_default_token, new_default_token);
  };

  RunLoopUntil([this]() {
    for (auto& it : devices_) {
      if (!it.second.is_removed) {
        return false;
      }
    }
    return true;
  });

  // Mute events, to avoid flakes from "unbind triggers an event elsewhere".
  audio_dev_enum_.events().OnDeviceAdded = nullptr;
  audio_dev_enum_.events().OnDeviceRemoved = nullptr;
  audio_dev_enum_.events().OnDeviceGainChanged = nullptr;
  audio_dev_enum_.events().OnDefaultDeviceChanged = nullptr;
}

void HermeticAudioTest::OnDeviceAdded(fuchsia::media::AudioDeviceInfo info) {
  auto id = info.unique_id;
  token_to_unique_id_[info.token_id] = id;
  if (info.is_input) {
    if (!devices_[id].input) {
      ADD_FAILURE() << "Unexpected arrival of input device " << id << ", no such device exists";
    }
    if (devices_[id].info != std::nullopt) {
      ADD_FAILURE() << "Duplicate arrival of input device " << id;
    }
    devices_[id].input->set_token(info.token_id);
  } else {
    if (!devices_[id].output) {
      ADD_FAILURE() << "Unexpected arrival of output device " << id << ", no such device exists";
    }
    if (devices_[id].info != std::nullopt) {
      ADD_FAILURE() << "Duplicate arrival of output device " << id;
    }
    devices_[id].output->set_token(info.token_id);
  }
  token_to_unique_id_[info.token_id] = id;
  devices_[id].info = info;
  AUDIO_LOG(DEBUG) << "Output device (token = " << info.token_id << ", id = " << id
                   << ") has been added";
}

void HermeticAudioTest::OnDefaultDeviceChanged(uint64_t old_default_token,
                                               uint64_t new_default_token) {
  // In case of multiple pending calls during SetUp, process most recent new_default_token.
  if (!initial_devices_received_) {
    pending_default_device_tokens_.push(new_default_token);
    return;
  }
  EXPECT_TRUE(old_default_token == 0 || token_to_unique_id_.count(old_default_token) > 0)
      << "Default device changed from unknown device " << old_default_token << " to "
      << new_default_token;

  EXPECT_TRUE(new_default_token == 0 || token_to_unique_id_.count(new_default_token) > 0)
      << "Default device changed from " << old_default_token << " to unknown device "
      << new_default_token;

  AUDIO_LOG(DEBUG) << "Default output device changed from " << old_default_token << " to "
                   << new_default_token;

  if (old_default_token != 0) {
    auto id = token_to_unique_id_[old_default_token];
    devices_[id].is_default = false;
  }
  if (new_default_token != 0) {
    auto id = token_to_unique_id_[new_default_token];
    devices_[id].is_default = true;
  }
}

fuchsia::media::AudioDeviceEnumeratorPtr HermeticAudioTest::TakeOwnershipOfAudioDeviceEnumerator() {
  FX_CHECK(devices_.empty());
  FX_CHECK(capturers_.empty());
  FX_CHECK(renderers_.empty());

  audio_dev_enum_.events().OnDeviceAdded = nullptr;
  audio_dev_enum_.events().OnDeviceRemoved = nullptr;
  audio_dev_enum_.events().OnDeviceGainChanged = nullptr;
  audio_dev_enum_.events().OnDefaultDeviceChanged = nullptr;

  return std::move(audio_dev_enum_);
}

void HermeticAudioTest::ExpectNoOverflowsOrUnderflows() {
  for (auto& [_, device] : devices_) {
    if (device.output) {
      ExpectInspectMetrics(device.output.get(),
                           {
                               .children =
                                   {
                                       {"device underflows", {.uints = {{"count", 0}}}},
                                       {"pipeline underflows", {.uints = {{"count", 0}}}},
                                   },
                           });
    }
  }
  for (auto& r : renderers_) {
    ExpectInspectMetrics(r.get(), {
                                      .children =
                                          {
                                              {"underflows", {.uints = {{"count", 0}}}},
                                          },
                                  });
  }
  for (auto& c : capturers_) {
    ExpectInspectMetrics(c.get(), {
                                      .children =
                                          {
                                              {"overflows", {.uints = {{"count", 0}}}},
                                          },
                                  });
  }
}

void HermeticAudioTest::ExpectInspectMetrics(VirtualOutputImpl* output,
                                             const ExpectedInspectProperties& props) {
  ExpectInspectMetrics({"output devices", fxl::StringPrintf("%03lu", output->inspect_id())}, props);
}

void HermeticAudioTest::ExpectInspectMetrics(VirtualInputImpl* input,
                                             const ExpectedInspectProperties& props) {
  ExpectInspectMetrics({"input devices", fxl::StringPrintf("%03lu", input->inspect_id())}, props);
}

void HermeticAudioTest::ExpectInspectMetrics(RendererShimImpl* renderer,
                                             const ExpectedInspectProperties& props) {
  ExpectInspectMetrics({"renderers", fxl::StringPrintf("%lu", renderer->inspect_id())}, props);
}

void HermeticAudioTest::ExpectInspectMetrics(CapturerShimImpl* capturer,
                                             const ExpectedInspectProperties& props) {
  ExpectInspectMetrics({"capturers", fxl::StringPrintf("%lu", capturer->inspect_id())}, props);
}

void HermeticAudioTest::ExpectInspectMetrics(const std::vector<std::string>& path,
                                             const ExpectedInspectProperties& props) {
  auto root = environment_->ReadInspect(HermeticAudioEnvironment::kAudioCoreComponent);
  auto path_string = fxl::JoinStrings(path, "/");
  auto h = root.GetByPath(path);
  if (!h) {
    ADD_FAILURE() << "Missing inspect hierarchy for " << path_string;
    return;
  }
  ExpectedInspectProperties::Check(props, path_string, *h);
}

// Explicitly instantiate all possible implementations.
#define INSTANTIATE(T)                                                                     \
  template VirtualOutput<T>* HermeticAudioTest::CreateOutput<T>(                           \
      const audio_stream_unique_id_t&, TypedFormat<T>, size_t,                             \
      std::optional<DevicePlugProperties>, float);                                         \
  template VirtualInput<T>* HermeticAudioTest::CreateInput<T>(                             \
      const audio_stream_unique_id_t&, TypedFormat<T>, size_t,                             \
      std::optional<DevicePlugProperties>, float);                                         \
  template AudioRendererShim<T>* HermeticAudioTest::CreateAudioRenderer<T>(                \
      TypedFormat<T>, size_t, fuchsia::media::AudioRenderUsage, std::optional<zx::clock>); \
  template AudioCapturerShim<T>* HermeticAudioTest::CreateAudioCapturer<T>(                \
      TypedFormat<T>, size_t, fuchsia::media::AudioCapturerConfiguration);                 \
  template UltrasoundRendererShim<T>* HermeticAudioTest::CreateUltrasoundRenderer<T>(      \
      TypedFormat<T>, size_t, bool);                                                       \
  template UltrasoundCapturerShim<T>* HermeticAudioTest::CreateUltrasoundCapturer<T>(      \
      TypedFormat<T>, size_t, bool);

INSTANTIATE_FOR_ALL_FORMATS(INSTANTIATE)

}  // namespace media::audio::test
