// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/utils.h"

#include <fuchsia/scheduler/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <cstdint>
#include <unordered_map>

#include "src/media/audio/audio_core/testing/fake_profile_provider.h"

namespace media::audio {
namespace {

class UtilsTest : public gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    TestLoopFixture::SetUp();
    auto svc = context_provider_.service_directory_provider();
    ASSERT_EQ(ZX_OK, svc->AddService(profile_provider_.GetHandler()));
  }

  FakeProfileProvider* profile_provider() { return &profile_provider_; }

  sys::ComponentContext* context() { return context_provider_.context(); }

 private:
  FakeProfileProvider profile_provider_;
  sys::testing::ComponentContextProvider context_provider_;
};

TEST_F(UtilsTest, AcquireAudioCoreImplProfile) {
  ASSERT_TRUE(profile_provider()->SetProfile(24));

  zx::profile profile;
  ASSERT_FALSE(profile);
  AcquireAudioCoreImplProfile(context(), [&profile](zx::profile p) { profile = std::move(p); });
  RunLoopUntilIdle();

  ASSERT_TRUE(profile);
}

TEST_F(UtilsTest, AcquireAudioCoreImplProfile_ProfileUnavailable) {
  zx::profile profile;
  bool callback_invoked = false;
  AcquireAudioCoreImplProfile(context(), [&](zx::profile p) {
    profile = std::move(p);
    callback_invoked = true;
  });
  RunLoopUntilIdle();

  ASSERT_FALSE(profile);
  ASSERT_TRUE(callback_invoked);
}

}  // namespace
}  // namespace media::audio
