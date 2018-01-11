// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/module/fidl/module_data.fidl.h"
#include "peridot/bin/story_runner/chain_impl.h"
#include "peridot/lib/gtest/test_with_message_loop.h"
#include "peridot/lib/testing/story_controller_mock.h"

namespace modular {
namespace {

class ChainImplTest : public gtest::TestWithMessageLoop {
 public:
  void Reset(fidl::Array<fidl::String> path,
             std::map<std::string, std::vector<std::string>> link_map) {
    auto chain_data = ChainData::New();
    for (const auto& entry : link_map) {
      auto link_path = LinkPath::New();
      link_path->module_path = fidl::Array<fidl::String>::From(entry.second);
      auto key_link_data = ChainKeyToLinkData::New();
      key_link_data->key = entry.first;
      key_link_data->link_path = std::move(link_path);
      chain_data->key_to_link_map.push_back(std::move(key_link_data));
    }
    impl_.reset(new ChainImpl(std::move(path), std::move(chain_data),
                              &story_controller_));
    impl_->Connect(chain_.NewRequest());
  }

 protected:
  ChainPtr chain_;
  std::unique_ptr<ChainImpl> impl_;

  StoryControllerMock story_controller_;
};

TEST_F(ChainImplTest, Empty) {
  Reset({"one", "two"}, {});

  // |chain_path()| is only available on ChainImpl.
  const auto& path = impl_->chain_path();
  ASSERT_EQ(2lu, path.size());
  EXPECT_EQ("one", path[0]);
  EXPECT_EQ("two", path[1]);

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

TEST_F(ChainImplTest, chain_path) {
  Reset({"one", "two"}, {});
}

TEST_F(ChainImplTest, GetKeys) {
  // We test the empty keys case above in Empty.
  Reset({"one", "two"},
        {{"key1", {"link", "path1"}}, {"key2", {"link", "path2"}}});

  bool done{false};
  chain_->GetKeys([&done](const fidl::Array<fidl::String>& keys) {
    done = true;
    ASSERT_EQ(2ul, keys.size());
    EXPECT_EQ("key1", keys[0]);
    EXPECT_EQ("key2", keys[1]);
  });
  ASSERT_TRUE(RunLoopUntil([&done] { return done; }));
}

TEST_F(ChainImplTest, GetLink) {
  // Show that the GetLink call is proxied to the StoryController.
  // StoryController owns all Links.
  Reset({"one", "two"},
        {{"key1", {"link", "path1"}}, {"key2", {"link", "path2"}}});

  LinkPtr link;
  chain_->GetLink("key1", link.NewRequest());
  ASSERT_TRUE(RunLoopUntil([this] () { return story_controller_.get_link_calls.size() > 0; }));
  ASSERT_EQ(1lu, story_controller_.get_link_calls.size());
  EXPECT_TRUE(story_controller_.get_link_calls[0].module_path.Equals(fidl::Array<fidl::String>{"link", "path1"}));
  EXPECT_FALSE(story_controller_.get_link_calls[0].name);

  link.reset();
  story_controller_.get_link_calls.clear();
  chain_->GetLink("key2", link.NewRequest());
  ASSERT_TRUE(RunLoopUntil([this] () { return story_controller_.get_link_calls.size() > 0; }));
  ASSERT_EQ(1lu, story_controller_.get_link_calls.size());
  EXPECT_TRUE(story_controller_.get_link_calls[0].module_path.Equals(fidl::Array<fidl::String>{"link", "path2"}));
  EXPECT_FALSE(story_controller_.get_link_calls[0].name);
}

}  // namespace
}  // namespace modular
