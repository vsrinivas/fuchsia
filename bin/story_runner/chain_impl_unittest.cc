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
    impl_.reset(new ChainImpl(std::move(path), std::move(chain_data)));
  }

 protected:
  std::unique_ptr<ChainImpl> impl_;
};

TEST_F(ChainImplTest, Empty) {
  Reset({"one", "two"}, {});

  const auto& path = impl_->chain_path();
  ASSERT_EQ(2lu, path.size());
  EXPECT_EQ("one", path[0]);
  EXPECT_EQ("two", path[1]);

  EXPECT_FALSE(impl_->GetLinkPathForKey("foo"));
}

TEST_F(ChainImplTest, GetLinkPath) {
  // Show that the GetLink call is proxied to the StoryController.
  // StoryController owns all Links.
  Reset({"one", "two"},
        {{"key1", {"link", "path1"}}, {"key2", {"link", "path2"}}});

  EXPECT_FALSE(impl_->GetLinkPathForKey("foo"));

  auto path = impl_->GetLinkPathForKey("key1");
  ASSERT_TRUE(path);
  EXPECT_TRUE(path->module_path.Equals(
      fidl::Array<fidl::String>{"link", "path1"}));

  path = impl_->GetLinkPathForKey("key2");
  ASSERT_TRUE(path);
  EXPECT_TRUE(path->module_path.Equals(
      fidl::Array<fidl::String>{"link", "path2"}));

}

}  // namespace
}  // namespace modular
