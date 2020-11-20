// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/test/api/fuzzer/fuzzed_client.h"
#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

class FuzzedTest : public HermeticAudioTest {
 public:
  FuzzedTest(const uint8_t* data, size_t size) : data_(data, size) {}

  void TestBody() {
    SetUp();
    bool all_done = false;
    while (data_.remaining_bytes() && !all_done) {
      all_done = true;
      for (auto& c : capturers_) {
        if (!c->Done()) {
          all_done = false;
          auto step_done = c->Step();
          if (data_.ConsumeBool()) {
            RunLoopUntil(std::move(step_done));
          }
        }
      }
      for (auto& r : renderers_) {
        if (!r->Done()) {
          all_done = false;
          auto step_done = r->Step();
          if (data_.ConsumeBool()) {
            RunLoopUntil(std::move(step_done));
          }
        }
      }
      RunLoopUntilIdle();
    }
    TearDown();
  }

 private:
  static const uint32_t kMaxRenderers = 3;
  static const uint32_t kMaxCapturers = 3;

  void SetUp() {
    HermeticAudioTest::SetUp();
    auto format = Format::Create<FuzzerConst::SampleFormat>(2, FuzzerConst::kFrameRate).value();
    // Setup output device.
    fake_output_ = CreateOutput({{0xff, 0x00}}, format, FuzzerConst::kFrameRate);
    // Setup input device.
    fake_input_ = CreateInput({{0xee, 0x00}}, format, FuzzerConst::kFrameRate);

    // Initialize random number of renderers less than kMaxRenderers.
    int num_renderers = data_.ConsumeIntegralInRange<uint8_t>(0, kMaxRenderers);
    for (int i = 0; i < num_renderers; ++i) {
      auto r = std::make_unique<FuzzedRenderer>(
          CreateAudioRenderer(format, FuzzerConst::kFrameRate), data_);
      RunLoopUntilIdle();
      renderers_.push_back(std::move(r));
    }

    // Initialize random number of capturers less than kMaxCapturers.
    int num_capturers = data_.ConsumeIntegralInRange<uint8_t>(0, kMaxCapturers);
    for (int i = 0; i < num_capturers; ++i) {
      auto loopback = data_.ConsumeBool();
      auto configuration = loopback ? fuchsia::media::AudioCapturerConfiguration::WithLoopback(
                                          fuchsia::media::LoopbackAudioCapturerConfiguration())
                                    : fuchsia::media::AudioCapturerConfiguration::WithInput(
                                          fuchsia::media::InputAudioCapturerConfiguration());
      auto c = std::make_unique<FuzzedCapturer>(
          CreateAudioCapturer(format, FuzzerConst::kFrameRate, std::move(configuration)), data_);
      RunLoopUntilIdle();
      capturers_.push_back(std::move(c));
    }
  }

  void TearDown() {
    for (auto& c : capturers_) {
      c->Unbind();
    }
    for (auto& r : renderers_) {
      r->Unbind();
    }
    RunLoopUntilIdle();
    HermeticAudioTest::TearDown();
  }

  FuzzedDataProvider data_;
  std::vector<std::unique_ptr<FuzzedCapturer>> capturers_;
  std::vector<std::unique_ptr<FuzzedRenderer>> renderers_;
  VirtualOutput<FuzzerConst::SampleFormat>* fake_output_ = nullptr;
  VirtualInput<FuzzerConst::SampleFormat>* fake_input_ = nullptr;
};
}  // namespace media::audio::test

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  media::audio::test::FuzzedTest fuzz(data, size);
  fuzz.TestBody();
  return 0;
}
