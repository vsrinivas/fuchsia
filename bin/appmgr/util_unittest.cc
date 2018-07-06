// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/util.h"

#include "gtest/gtest.h"

namespace component {
namespace {

TEST(UtilTests, GetLabelFromURL) {
  std::string values[][2] = {
      {"", ""},        {"foo/bar", "bar"},  {"foo/bar/", "foo/bar/"},
      {"/foo", "foo"}, {"/foo/bar", "bar"}, {"foo", "foo"},
      {"foo/", "foo/"}};
  for (auto value : values) {
    auto& url = value[0];
    auto& expected = value[1];
    EXPECT_EQ(Util::GetLabelFromURL(url), expected) << "for url: " << url;
  }
}

TEST(UtilTests, GetArgsString) {
  ::fidl::VectorPtr<::fidl::StringPtr> null_vec;
  EXPECT_EQ(Util::GetArgsString(null_vec), "");

  ::fidl::VectorPtr<::fidl::StringPtr> empty_vec(3);
  EXPECT_EQ(Util::GetArgsString(empty_vec), "  ");

  ::fidl::VectorPtr<::fidl::StringPtr> vec;
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

  channels.client_request.read(0, got1, sizeof(got1), nullptr, nullptr, 0,
                               nullptr);
  launchInfo.directory_request.read(0, got2, sizeof(got2), nullptr, nullptr, 0,
                                    nullptr);

  EXPECT_STREQ(got1, msg1);
  EXPECT_STREQ(got2, msg2);
}

class RestartBackOffTester : public RestartBackOff {
 public:
  RestartBackOffTester(zx::duration min_backoff, zx::duration max_backoff,
                       zx::duration alive_reset_limit)
      : RestartBackOff(min_backoff, max_backoff, alive_reset_limit) {}
  void SetCurrentTime(zx::time time) { current_time_ = time; }

 protected:
  zx::time GetCurrentTime() const override { return current_time_; }

 private:
  zx::time current_time_;
};

TEST(RestartBackOffTest, WaitIncreasesAndResets) {
  auto start_time = zx::time() + zx::hour(10);
  auto within_limit = start_time + zx::msec(100);
  auto after_limit = start_time + zx::msec(1100);
  RestartBackOffTester util(zx::sec(1), zx::sec(10), zx::sec(1));
  util.SetCurrentTime(start_time);
  util.Start();
  util.SetCurrentTime(within_limit);
  EXPECT_GE(util.GetNext(), zx::sec(1));
  for (int i = 0; i < 10; i++) {
    util.GetNext();
  }
  // Ensure we don't go over the limit.
  EXPECT_EQ(util.GetNext(), zx::sec(10));

  // If the task succeeded for long enough, ensure backoff is reset.
  util.SetCurrentTime(after_limit);
  EXPECT_LT(util.GetNext(), zx::sec(10));
}

}  // namespace
}  // namespace component
