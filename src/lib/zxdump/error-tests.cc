// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxdump/types.h>

#include <cerrno>
#include <sstream>

#include <gtest/gtest.h>

namespace {

TEST(ZxdumpTests, OstreamErrorFormat) {
#ifdef __Fuchsia__
  constexpr std::string_view inval_str = "ZX_ERR_INVALID_ARGS";
#else  // status_string() is not available on host.
  std::string inval_str = std::string("error ") + std::to_string(ZX_ERR_INVALID_ARGS);
#endif

  const zxdump::Error e = {.op_ = "foo", .status_ = ZX_ERR_INVALID_ARGS};
#ifdef __Fuchsia__
  EXPECT_EQ(e.status_string(), inval_str);
#endif

  const std::string expected = std::string("foo: ") + std::string(inval_str);
  std::stringstream s;
  s << e;
  EXPECT_EQ(s.str(), expected);
}

TEST(ZxdumpTests, OstreamFdErrorFormat) {
  {
    constexpr zxdump::FdError e = {.op_ = "foo"};
    EXPECT_EQ(e.error_string(), strerror(0));

    std::stringstream s;
    s << e;
    EXPECT_EQ(s.str(), "foo");
  }
  {
    constexpr zxdump::FdError e = {.op_ = "foo", .error_ = EINVAL};
    EXPECT_EQ(e.error_string(), strerror(EINVAL));

    std::string foo_einval = std::string("foo: ") + strerror(EINVAL);
    std::stringstream s;
    s << e;
    EXPECT_EQ(s.str(), foo_einval);
  }
}

}  // namespace
