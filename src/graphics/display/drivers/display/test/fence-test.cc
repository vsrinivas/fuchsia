// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../fence.h"

#include <lib/async-testing/test_loop.h>
#include <lib/async/default.h>

#include <zxtest/zxtest.h>

namespace display {

class TestCallback : public FenceCallback {
 public:
  void OnFenceFired(FenceReference* f) override { fired_.push_back(f); }

  void OnRefForFenceDead(Fence* fence) override { fence->OnRefDead(); }

  fbl::Vector<FenceReference*> fired_;
};

class FenceTest : public zxtest::Test {
 public:
  void SetUp() override {
    zx::event ev;
    zx::event::create(0, &ev);
    fence_ = fbl::AdoptRef(new Fence(&cb_, loop_.dispatcher(), 1, std::move(ev)));
  }

  void TearDown() override { fence_->ClearRef(); }

  async::TestLoop& loop() { return loop_; }
  fbl::RefPtr<Fence> fence() { return fence_; }
  TestCallback& cb() { return cb_; }

 private:
  async::TestLoop loop_;
  fbl::RefPtr<Fence> fence_;
  TestCallback cb_;
};

TEST_F(FenceTest, MultipleRefs_OnePurpose) {
  fence()->CreateRef();
  auto one = fence()->GetReference();
  auto two = fence()->GetReference();
}

TEST_F(FenceTest, MultipleRefs_MultiplePurposes) {
  fence()->CreateRef();
  auto one = fence()->GetReference();
  fence()->CreateRef();
  auto two = fence()->GetReference();
  fence()->CreateRef();
  auto three = fence()->GetReference();
  two->StartReadyWait();
  one->StartReadyWait();

  three->Signal();
  loop().RunUntilIdle();

  three->Signal();
  loop().RunUntilIdle();

  ASSERT_EQ(cb().fired_.size(), 2);
  EXPECT_EQ(cb().fired_[0], two.get());
  EXPECT_EQ(cb().fired_[1], one.get());
}

}  // namespace display
