// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/standalone-test/standalone.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/sanitizer.h>

#include <string>

#include <zxtest/zxtest.h>

#include "helper.h"

namespace {

class MultipleProcessSvcTest : public zxtest::Test {
 public:
  void SetUp() final {
    static zx::channel svc_stash;
    static zx::channel svc_server;
    static zx::channel provider_svc_server;

    // Prevents multiple iterations from discarding this.
    if (!svc_stash) {
      svc_stash = GetSvcStash();
      ASSERT_TRUE(svc_stash);
    }

    // Order matters, provider is stored first.
    if (!provider_svc_server) {
      ASSERT_NO_FATAL_FAILURE(GetStashedSvc(svc_stash.borrow(), provider_svc_server));
    }

    // Ours is stored second.
    if (!svc_server) {
      ASSERT_NO_FATAL_FAILURE(GetStashedSvc(svc_stash.borrow(), svc_server));
    }

    svc_stash_ = svc_stash.borrow();
    svc_ = standalone::GetNsDir("/svc");
    stashed_svc_ = svc_server.borrow();
    provider_svc_ = provider_svc_server.borrow();
  }

  zx::unowned_channel svc_stash() { return svc_stash_->borrow(); }
  zx::unowned_channel local_svc() { return svc_->borrow(); }
  zx::unowned_channel stashed_svc() { return stashed_svc_->borrow(); }
  zx::unowned_channel provider_svc() { return provider_svc_->borrow(); }

 private:
  zx::unowned_channel svc_stash_;
  zx::unowned_channel svc_;
  zx::unowned_channel stashed_svc_;
  zx::unowned_channel provider_svc_;
};

// The provided data is in 'data-publisher/main.cc'.
TEST_F(MultipleProcessSvcTest, ProviderDataMatchesExpectations) {
  Message debug_msg;
  ASSERT_NO_FAILURES(GetDebugDataMessage(provider_svc(), debug_msg));
  DebugDataMessageView view(debug_msg);

  ASSERT_EQ(view.sink(), "data-provider");

  // Token must be signaled with peer closed.
  zx_signals_t observed = 0;
  view.token()->wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite_past(), &observed);
  EXPECT_TRUE((observed & ZX_EVENTPAIR_PEER_CLOSED) != 0);

  // Check vmo contents.
  std::array<char, 12> actual_contents = {};
  ASSERT_OK(view.vmo()->read(actual_contents.data(), 0, actual_contents.size()));

  ASSERT_EQ(std::string(actual_contents.data(), actual_contents.size()), "Hello World!");
}

TEST_F(MultipleProcessSvcTest, SanitizerPublishDataShowsUpInStashedHandle) {
  ASSERT_TRUE(local_svc()->is_valid());
  ASSERT_TRUE(stashed_svc()->is_valid());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(124, 0, &vmo));

  // Channel transitions from not readable to readable.
  zx_signals_t observed = 0;
  ASSERT_STATUS(stashed_svc()->wait_one(ZX_CHANNEL_READABLE, zx::time::infinite_past(), &observed),
                ZX_ERR_TIMED_OUT);

  constexpr const char* kDataSink = "some_sink_name";
  zx_koid_t vmo_koid = GetKoid(vmo.get());

  zx::eventpair token_1(__sanitizer_publish_data(kDataSink, vmo.release()));

  zx_koid_t token_koid = GetPeerKoid(token_1.get());
  ASSERT_NE(token_koid, ZX_KOID_INVALID);
  ASSERT_NE(vmo_koid, ZX_KOID_INVALID);

  observed = 0;
  ASSERT_OK(stashed_svc()->wait_one(ZX_CHANNEL_READABLE, zx::time::infinite_past(), &observed));
  ASSERT_TRUE((observed & ZX_CHANNEL_READABLE) != 0);

  // There should be an open request after this.
  Message debug_msg;
  ASSERT_NO_FAILURES(GetDebugDataMessage(stashed_svc(), debug_msg));
  DebugDataMessageView view(debug_msg);
  ASSERT_EQ(view.sink(), kDataSink);
  ASSERT_EQ(GetKoid(view.vmo()->get()), vmo_koid);
  ASSERT_EQ(GetKoid(view.token()->get()), token_koid);
}
}  // namespace
