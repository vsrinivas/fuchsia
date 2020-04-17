// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/usage_gain_reporter_impl.h"

#include <lib/fidl/cpp/binding.h>

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/testing/threading_model_fixture.h"

namespace media::audio {
namespace {

class FakeGainListener : public fuchsia::media::UsageGainListener {
 public:
  fidl::InterfaceHandle<fuchsia::media::UsageGainListener> NewBinding() {
    return binding_.NewBinding();
  }

  bool muted() const { return last_muted_; }

  float gain_dbfs() const { return last_gain_dbfs_; }

  size_t call_count() const { return call_count_; }

 private:
  // |fuchsia::media::UsageGainListener|
  void OnGainMuteChanged(bool muted, float gain_dbfs, OnGainMuteChangedCallback callback) final {
    last_muted_ = muted;
    last_gain_dbfs_ = gain_dbfs;
    call_count_++;
  }

  fidl::Binding<fuchsia::media::UsageGainListener> binding_{this};
  bool last_muted_ = false;
  float last_gain_dbfs_ = 0.0;
  size_t call_count_ = 0;
};

class UsageGainReporterTest : public testing::ThreadingModelFixture {
 public:
  UsageGainReporterTest()
      : ThreadingModelFixture(ProcessConfigBuilder()
                                  .SetDefaultVolumeCurve(VolumeCurve::DefaultForMinGain(-60.0))
                                  .Build()) {}
};

TEST_F(UsageGainReporterTest, UpdatesSingleListener) {
  UsageGainReporterImpl under_test(&context());

  const std::string device_id = "fake_device_id";
  const auto usage =
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA);

  auto fake_listener = std::make_unique<FakeGainListener>();
  under_test.RegisterListener(device_id, fidl::Clone(usage), fake_listener->NewBinding());

  const float expected_gain_dbfs = -10.0;
  context().volume_manager().SetUsageGain(fidl::Clone(usage), expected_gain_dbfs);

  RunLoopUntilIdle();

  EXPECT_FLOAT_EQ(fake_listener->gain_dbfs(), expected_gain_dbfs);
  EXPECT_EQ(fake_listener->call_count(), 2u);
}

}  // namespace
}  // namespace media::audio
