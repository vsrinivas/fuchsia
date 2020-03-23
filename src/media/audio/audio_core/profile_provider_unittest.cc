// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/profile_provider.h"

#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/testing/fake_profile_provider.h"

namespace media::audio {

class ProfileProviderTest : public gtest::TestLoopFixture {
  void SetUp() override {
    TestLoopFixture::SetUp();
    profile_provider_ = std::make_unique<ProfileProvider>(*context());

    auto svc = context_provider_.service_directory_provider();
    ASSERT_EQ(ZX_OK, svc->AddService(fake_profile_provider_.GetHandler()));
  }

 protected:
  sys::ComponentContext* context() { return context_provider_.context(); }
  FakeProfileProvider fake_profile_provider_;
  std::unique_ptr<ProfileProvider> profile_provider_;
  sys::testing::ComponentContextProvider context_provider_;
};

TEST_F(ProfileProviderTest, CallRegisterHandler) {
  bool called = false;
  ASSERT_TRUE(fake_profile_provider_.SetProfile(24));
  zx::thread self;
  ASSERT_EQ(zx::thread::self()->duplicate(ZX_RIGHT_SAME_RIGHTS, &self), ZX_OK);
  profile_provider_->RegisterHandler(std::move(self), "test", zx::duration(10000000).to_nsecs(),
                                     [&called](uint64_t period, uint64_t capacity) {
                                       ASSERT_EQ(period, 0UL);
                                       ASSERT_EQ(capacity, 0UL);
                                       called = true;
                                     });
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
}

}  // namespace media::audio
