// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_TESTING_TEST_EFFECTS_V1_H_
#define SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_TESTING_TEST_EFFECTS_V1_H_

#include <lib/syslog/cpp/macros.h>

#include <memory>
#include <string_view>

#include "src/media/audio/effects/test_effects/test_effects_v1.h"

namespace media::audio::testing {

static constexpr char kTestEffectsModuleName[] = "test_effects_v1.so";

// Opens the 'extension' interface to the test_effects module. This is an auxiliary ABI in addition
// to the Fuchsia Effects ABI that allows the behavior of the test_effects module to be controlled
// by tests.
//
// To use this correctly, you must also have included //src/media/audio/effects/test_effects as a
// loadable_module in the test_package that is linking against this library.
//
// See //src/media/audio/lib/effects_loader:audio_effects_loader_unittests as an example.
std::shared_ptr<test_effects_v1_module_ext> OpenTestEffectsExt();

class TestEffectV1Builder {
 public:
  TestEffectV1Builder(std::shared_ptr<test_effects_v1_module_ext> module, std::string_view name)
      : module_(std::move(module)) {
    auto length = std::min(name.length(), FUCHSIA_AUDIO_EFFECTS_MAX_NAME_LENGTH - 1);
    memcpy(spec_.description.name, name.data(), length);
    spec_.description.name[length] = 0;
  }

  ~TestEffectV1Builder() {
    if (module_) {
      zx_status_t status = Build();
      if (status != ZX_OK) {
        FX_PLOGS(FATAL, status) << "Failed to add audio effect";
      }
    }
  }

  TestEffectV1Builder& WithAction(EffectAction action, float value) {
    spec_.action = action;
    spec_.value = value;
    return *this;
  }

  TestEffectV1Builder& WithBlockSize(int64_t block_size) {
    spec_.block_size_frames = static_cast<uint32_t>(block_size);
    return *this;
  }

  TestEffectV1Builder& WithMaxFramesPerBuffer(int64_t max_frames_per_buffer) {
    spec_.max_batch_size = static_cast<uint32_t>(max_frames_per_buffer);
    return *this;
  }

  TestEffectV1Builder& WithSignalLatencyFrames(int64_t latency) {
    spec_.signal_latency_frames = static_cast<uint32_t>(latency);
    return *this;
  }

  TestEffectV1Builder& WithRingOutFrames(int64_t ring_out_frames) {
    spec_.ring_out_frames = static_cast<uint32_t>(ring_out_frames);
    return *this;
  }

  TestEffectV1Builder& WithChannelization(int32_t channels_in, int32_t channels_out) {
    spec_.description.incoming_channels = static_cast<uint16_t>(channels_in);
    spec_.description.outgoing_channels = static_cast<uint16_t>(channels_out);
    return *this;
  }

  zx_status_t Build() {
    if (!module_) {
      return ZX_ERR_BAD_STATE;
    }
    zx_status_t status = module_->add_effect(spec_);
    module_ = nullptr;
    return status;
  }

 private:
  test_effect_v1_spec spec_ = {
      .description =
          {
              .name = "",
              .incoming_channels = FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
              .outgoing_channels = FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN,
          },
      .block_size_frames = FUCHSIA_AUDIO_EFFECTS_BLOCK_SIZE_ANY,
      .max_batch_size = FUCHSIA_AUDIO_EFFECTS_FRAMES_PER_BUFFER_ANY,
      .signal_latency_frames = 0,
      .ring_out_frames = 0,
      .action = TEST_EFFECTS_ACTION_ADD,
      .value = 0.0f,
  };
  std::shared_ptr<test_effects_v1_module_ext> module_;
};

class TestEffectsV1Module {
 public:
  static TestEffectsV1Module Open() { return TestEffectsV1Module(OpenTestEffectsExt()); }

  TestEffectsV1Module(std::shared_ptr<test_effects_v1_module_ext> module)
      : module_(std::move(module)) {}

  // Disallow copy/move.
  TestEffectsV1Module(const TestEffectsV1Module&) = delete;
  TestEffectsV1Module& operator=(const TestEffectsV1Module&) = delete;
  TestEffectsV1Module(TestEffectsV1Module&& o) = delete;
  TestEffectsV1Module& operator=(TestEffectsV1Module&& o) = delete;

  ~TestEffectsV1Module() {
    zx_status_t status = ClearEffects();
    if (status != ZX_OK) {
      FX_PLOGS(FATAL, status) << "Failed to clear audio effects";
    }
  }

  // Creates a new effect for the library. Must be called while the number of active effect
  // instances is zero.
  TestEffectV1Builder AddEffect(std::string_view name) const {
    return TestEffectV1Builder(module_, name);
  }

  // Removes all effects. Must be called while the number of active effect instances is zero.
  zx_status_t ClearEffects() const { return module_->clear_effects(); }

  // Returns the number of active effect instances owned by this module.
  uint32_t InstanceCount() const { return module_->num_instances(); }

  // Provides detailed information about a single effect instance.
  zx_status_t InspectInstance(fuchsia_audio_effects_handle_t effects_handle,
                              test_effects_v1_inspect_state* out) const {
    return module_->inspect_instance(effects_handle, out);
  }

 private:
  std::shared_ptr<test_effects_v1_module_ext> module_;
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_TESTING_TEST_EFFECTS_V1_H_
