// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/settings.h"

#include "gtest/gtest.h"
#include "lib/fxl/command_line.h"

namespace tracing {
namespace {

TEST(LogSettings, ParseValidOptions) {
  TraceSettings settings;

  settings.provider_label = "default";
  EXPECT_TRUE(ParseTraceSettings(fxl::CommandLineFromInitializerList({"argv0"}),
                                 &settings));
  EXPECT_EQ("default", settings.provider_label);

  settings.provider_label = "default";
  EXPECT_TRUE(ParseTraceSettings(
      fxl::CommandLineFromInitializerList({"argv0", "--trace-label"}),
      &settings));
  EXPECT_EQ("", settings.provider_label);

  settings.provider_label = "default";
  EXPECT_TRUE(ParseTraceSettings(
      fxl::CommandLineFromInitializerList({"argv0", "--trace-label=traceme"}),
      &settings));
  EXPECT_EQ("traceme", settings.provider_label);
}

}  // namespace
}  // namespace tracing
