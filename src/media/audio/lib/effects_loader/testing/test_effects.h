// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_TESTING_TEST_EFFECTS_H_
#define SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_TESTING_TEST_EFFECTS_H_

#include <memory>
#include <string_view>

#include "src/lib/syslog/cpp/logger.h"
#include "src/media/audio/effects/test_effects/test_effects.h"

namespace media::audio::testing {

static constexpr char kTestEffectsModuleName[] = "test_effects.so";

// Opens the 'extension' interface to the test_effects module. This is an auxiliary ABI in addition
// to the Fuchsia Effects ABI that allows the behavior of the test_effects module to be controlled
// by tests.
//
// To use this correctly, you must also have included //src/media/audio/effects/test_effects as a
// loadable_module in the test_package that is linking against this library.
//
// See //src/media/audio/lib/effects_loader:audio_effects_loader_unittests as an example.
std::shared_ptr<test_effects_module_ext> OpenTestEffectsExt();

class TestEffectBuilder {
 public:
  TestEffectBuilder(std::shared_ptr<test_effects_module_ext> module, std::string_view name)
      : module_(std::move(module)) {
    auto length = std::min(name.length(), FUCHSIA_AUDIO_EFFECTS_MAX_NAME_LENGTH - 1);
    memcpy(spec_.description.name, name.data(), length);
    spec_.description.name[length] = 0;
  }

  ~TestEffectBuilder() {
    if (module_) {
      zx_status_t status = Build();
      if (status != ZX_OK) {
        FX_PLOGS(FATAL, status) << "Failed to add audio effect";
      }
    }
  }

  TestEffectBuilder& WithAction(EffectAction action, float value) {
    spec_.action = action;
    spec_.value = value;
    return *this;
  }

  TestEffectBuilder& WithBlockSize(uint32_t block_size) {
    spec_.block_size_frames = block_size;
    return *this;
  }

  TestEffectBuilder& WithMaxFramesPerBuffer(uint32_t max_frames_per_buffer) {
    spec_.max_batch_size = max_frames_per_buffer;
    return *this;
  }

  TestEffectBuilder& WithSignalLatencyFrames(uint32_t latency) {
    spec_.signal_latency_frames = latency;
    return *this;
  }

  TestEffectBuilder& WithChannelization(uint16_t channels_in, uint16_t channels_out) {
    spec_.description.incoming_channels = channels_in;
    spec_.description.outgoing_channels = channels_out;
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
  test_effect_spec spec_ = {
      .description =
          {
              .name = "",
              .incoming_channels = FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
              .outgoing_channels = FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN,
          },
      .block_size_frames = FUCHSIA_AUDIO_EFFECTS_BLOCK_SIZE_ANY,
      .max_batch_size = FUCHSIA_AUDIO_EFFECTS_FRAMES_PER_BUFFER_ANY,
      .signal_latency_frames = 0,
      .action = TEST_EFFECTS_ACTION_ADD,
      .value = 0.0f,
  };
  std::shared_ptr<test_effects_module_ext> module_;
};

class TestEffectsModule {
 public:
  static TestEffectsModule Open() { return TestEffectsModule(OpenTestEffectsExt()); }

  TestEffectsModule(std::shared_ptr<test_effects_module_ext> module) : module_(std::move(module)) {}

  // Disallow copy/move.
  TestEffectsModule(const TestEffectsModule&) = delete;
  TestEffectsModule& operator=(const TestEffectsModule&) = delete;
  TestEffectsModule(TestEffectsModule&& o) = delete;
  TestEffectsModule& operator=(TestEffectsModule&& o) = delete;

  ~TestEffectsModule() { ClearEffects(); }

  // Creates a new effect for the library. Must be called while the number of active effect
  // instances is zero.
  TestEffectBuilder AddEffect(std::string_view name) const {
    return TestEffectBuilder(module_, name);
  }

  // Removes all effects. Must be called while the number of active effect instances is zero.
  zx_status_t ClearEffects() const { return module_->clear_effects(); }

  // Returns the number of active effect instances owned by this module.
  uint32_t InstanceCount() const { return module_->num_instances(); }

  // Provides detailed information about a single effect instance.
  zx_status_t InspectInstance(fuchsia_audio_effects_handle_t effects_handle,
                              test_effects_inspect_state* out) const {
    return module_->inspect_instance(effects_handle, out);
  }

 private:
  std::shared_ptr<test_effects_module_ext> module_;
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_TESTING_TEST_EFFECTS_H_
