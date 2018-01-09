// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "peridot/bin/story_runner/chain_impl.h"
#include "peridot/lib/gtest/test_with_message_loop.h"

namespace modular {
namespace {

class ChainImplTest : public gtest::TestWithMessageLoop {
 public:
  void Reset(fidl::Array<fidl::String> path) {
    impl_.reset(new ChainImpl(std::move(path)));
    impl_->Connect(chain_.NewRequest());
  }

 protected:
  ChainPtr chain_;
  std::unique_ptr<ChainImpl> impl_;
};

TEST_F(ChainImplTest, All) {
  Reset({"one", "two"});

  bool done{false};
  chain_->GetKeys([&done](const fidl::Array<fidl::String>& keys) {
    done = true;
    EXPECT_EQ(0ul, keys.size());
  });
  ASSERT_TRUE(RunLoopUntil([&done] { return done; }));

  bool saw_error{false};
  LinkPtr link;
  chain_->GetLink("someKey", link.NewRequest());
  link.set_connection_error_handler([&saw_error] { saw_error = true; });
  ASSERT_TRUE(RunLoopUntil([&saw_error] { return saw_error; }));
}

}  // namespace
}  // namespace modular
