// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace2json/trace_parser.h"

#include <gtest/gtest.h>

#include <sstream>

namespace {

TEST(TraceParserTest, InvalidTrace) {
  std::istringstream input("asdfasdfasdfasdfasdf");
  std::ostringstream output;

  {
    tracing::FuchsiaTraceParser parser(&output);
    EXPECT_FALSE(parser.ParseComplete(&input));
  }
}

}  // namespace
