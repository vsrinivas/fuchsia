// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/util.h"

#include <gtest/gtest.h>

namespace component {
namespace {

TEST(UtilTests, GetLabelFromURL) {
  std::string values[][2] = {{"", ""},        {"foo/bar", "bar"},  {"foo/bar/", "foo/bar/"},
                             {"/foo", "foo"}, {"/foo/bar", "bar"}, {"foo", "foo"},
                             {"foo/", "foo/"}};
  for (auto value : values) {
    auto& url = value[0];
    auto& expected = value[1];
    EXPECT_EQ(Util::GetLabelFromURL(url), expected) << "for url: " << url;
  }
}

TEST(UtilTests, GetArgsString) {
  ::fidl::VectorPtr<::std::string> null_vec;
  EXPECT_EQ(Util::GetArgsString(null_vec), "");

  ::fidl::VectorPtr<::std::string> empty_vec(3);
  EXPECT_EQ(Util::GetArgsString(empty_vec), "  ");

  std::vector<::std::string> vec;
  vec.push_back("foo");
  EXPECT_EQ(Util::GetArgsString(vec), "foo");
  vec.push_back("bar");
  EXPECT_EQ(Util::GetArgsString(vec), "foo bar");
  vec.push_back("blah");
  EXPECT_EQ(Util::GetArgsString(vec), "foo bar blah");
}

TEST(UtilTests, BindDirectory) {
  zx::channel dir, dir_req;
  ASSERT_EQ(zx::channel::create(0, &dir, &dir_req), ZX_OK);
  fuchsia::sys::LaunchInfo launchInfo;
  launchInfo.directory_request = std::move(dir_req);
  auto channels = Util::BindDirectory(&launchInfo);
  ASSERT_TRUE(channels.exported_dir.is_valid());
  ASSERT_TRUE(channels.client_request.is_valid());

  const char* msg1 = "message1";
  dir.write(0, msg1, strlen(msg1) + 1, nullptr, 0);

  const char* msg2 = "message2";
  channels.exported_dir.write(0, msg2, strlen(msg2) + 1, nullptr, 0);

  char got1[strlen(msg1) + 1];
  char got2[strlen(msg2) + 1];

  channels.client_request.read(0, got1, nullptr, sizeof(got1), 0, nullptr, nullptr);
  launchInfo.directory_request.read(0, got2, nullptr, sizeof(got2), 0, nullptr, nullptr);

  EXPECT_STREQ(got1, msg1);
  EXPECT_STREQ(got2, msg2);
}

}  // namespace
}  // namespace component
