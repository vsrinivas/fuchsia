// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/shared/profile_acquirer.h"

#include <fuchsia/scheduler/cpp/fidl.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <zircon/errors.h>

#include <cstdint>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/media/audio/audio_core/shared/testing/fake_profile_provider.h"

namespace media::audio {
namespace {

class ProfileAcquirerTest : public gtest::TestLoopFixture {
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

TEST_F(ProfileAcquirerTest, AcquireAudioCoreImplProfile) {
  ASSERT_TRUE(profile_provider()->SetProfile(24));
  zx_status_t status = ZX_ERR_NOT_FOUND;

  AcquireAudioCoreImplProfile(context(), [&status](zx_status_t s, zx::profile p) { status = s; });
  RunLoopUntilIdle();

  ASSERT_EQ(status, ZX_OK);
}

TEST_F(ProfileAcquirerTest, AcquireAudioCoreImplProfile_ProfileUnavailable) {
  bool callback_invoked = false;
  zx_status_t status = ZX_ERR_NOT_FOUND;
  AcquireAudioCoreImplProfile(context(), [&](zx_status_t s, zx::profile p) {
    status = s;
    callback_invoked = true;
  });
  RunLoopUntilIdle();

  ASSERT_NE(status, ZX_OK);
  ASSERT_TRUE(callback_invoked);
}

}  // namespace
}  // namespace media::audio
