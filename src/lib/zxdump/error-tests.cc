// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxdump/types.h>

#include <sstream>

#include <gtest/gtest.h>

namespace {

#ifdef __Fuchsia__  // status_string() is not available on host.
TEST(ZxdumpTests, OstreamErrorFormat) {
  constexpr zxdump::Error e{"foo", ZX_ERR_INVALID_ARGS};
  EXPECT_EQ(e.status_string(), "ZX_ERR_INVALID_ARGS");

  std::stringstream s;
  s << e;
  EXPECT_EQ(s.str(), "foo: ZX_ERR_INVALID_ARGS");
}
#endif

}  // namespace
