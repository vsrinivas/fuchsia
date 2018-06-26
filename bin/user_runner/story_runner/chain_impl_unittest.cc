// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/story_runner/chain_impl.h"

#include <memory>

#include <fuchsia/modular/cpp/fidl.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fsl/types/type_converters.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/type_converter.h"
#include "peridot/lib/testing/story_controller_mock.h"

namespace modular {
namespace {

class ChainImplTest : public testing::Test {
 public:
  void Reset(std::vector<fidl::StringPtr> path,
             std::map<std::string, std::vector<std::string>> link_map) {
    fuchsia::modular::ModuleParameterMap map;
    for (const auto& entry : link_map) {
      fuchsia::modular::LinkPath link_path;
      link_path.module_path =
          fxl::To<fidl::VectorPtr<fidl::StringPtr>>(entry.second);
      fuchsia::modular::ModuleParameterMapEntry map_entry;
      map_entry.name = entry.first;
      map_entry.link_path = std::move(link_path);
      map.entries.push_back(std::move(map_entry));
    }
    fidl::VectorPtr<fidl::StringPtr> tmp_path(std::move(path));
    impl_.reset(new ChainImpl(std::move(tmp_path), std::move(map)));
  }

 protected:
  std::unique_ptr<ChainImpl> impl_;
};

TEST_F(ChainImplTest, Empty) {
  Reset({"one", "two"}, {});

  const auto& path = impl_->chain_path();
  ASSERT_EQ(2lu, path->size());
  EXPECT_EQ("one", path->at(0));
  EXPECT_EQ("two", path->at(1));

  EXPECT_FALSE(impl_->GetLinkPathForParameterName("foo"));
}

TEST_F(ChainImplTest, GetLinkPath) {
  // Show that the GetLink call is proxied to the
  // fuchsia::modular::StoryController. fuchsia::modular::StoryController owns
  // all Links.
  Reset({"one", "two"},
        {{"key1", {"link", "path1"}}, {"key2", {"link", "path2"}}});

  EXPECT_FALSE(impl_->GetLinkPathForParameterName("foo"));

  auto path = impl_->GetLinkPathForParameterName("key1");
  ASSERT_TRUE(path);
  fidl::VectorPtr<fidl::StringPtr> expected;
  expected.reset({"link", "path1"});
  EXPECT_EQ(expected, path->module_path);

  path = impl_->GetLinkPathForParameterName("key2");
  ASSERT_TRUE(path);
  expected.reset({"link", "path2"});
  EXPECT_EQ(expected, path->module_path);
}

}  // namespace
}  // namespace modular
