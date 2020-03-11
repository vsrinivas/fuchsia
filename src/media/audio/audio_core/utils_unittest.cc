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

namespace media::audio {
namespace {

class FakeProfileProvider : public fuchsia::scheduler::ProfileProvider {
 public:
  fidl::InterfaceRequestHandler<fuchsia::scheduler::ProfileProvider> GetHandler() {
    return bindings_.GetHandler(this);
  }

  // |GetProfile| will return ZX_ERR_NOT_FOUND/ZX_HANDLE_INVALID for any priority that has not
  // previously been marked as valid with a call to |SetProfile|.
  //
  // Note that currently the only works for a single |GetProfile| call since we don't duplicate
  // a new handle before sending it back to the client.
  bool SetProfile(uint32_t priority) {
    // Since there's no easy way to create a profile handle in a test context, we'll just use an
    // event handle. This will be sufficient to allow the handle to be sent over the channel back
    // to the caller, but it will obviously not work of the caller is doing anything that requires
    // a zx::profile. This limitation is sufficient for the purposes of our tests.
    zx::event e;
    zx::event::create(0, &e);
    return profiles_by_priority_.insert({priority, zx::profile(e.release())}).second;
  }

 private:
  // |fuchsia::scheduler::ProfileProvider|
  void GetProfile(uint32_t priority, std::string name, GetProfileCallback callback) override {
    auto it = profiles_by_priority_.find(priority);
    if (it == profiles_by_priority_.end()) {
      callback(ZX_ERR_NOT_FOUND, zx::profile());
    } else {
      callback(ZX_OK, std::move(it->second));
    }
  }

  // |fuchsia::scheduler::ProfileProvider|
  // TODO(eieio): Temporary until the deadline scheduler fully lands in tree.
  void GetDeadlineProfile(uint64_t capacity, uint64_t deadline, uint64_t period, std::string name,
                          GetProfileCallback callback) override {}

  // |fuchsia::scheduler::ProfileProvider|
  void GetCpuAffinityProfile(fuchsia::scheduler::CpuSet cpu_mask,
                             GetProfileCallback callback) override {}

  std::unordered_map<uint32_t, zx::profile> profiles_by_priority_;
  fidl::BindingSet<fuchsia::scheduler::ProfileProvider> bindings_;
};

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
