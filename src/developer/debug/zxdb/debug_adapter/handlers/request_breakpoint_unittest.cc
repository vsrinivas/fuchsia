// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/debug_adapter/context_test.h"

namespace zxdb {

namespace {

class RequestBreakpointTest : public DebugAdapterContextTest {};

}  // namespace

TEST_F(RequestBreakpointTest, SetBreakpoints) {
  InitializeDebugging();

  // Send breakpoint request from the client.
  dap::SetBreakpointsRequest req = {};
  req.source.name = "i2c.c";
  req.source.path = "/fuchsia/src/devices/i2c/bin/i2c/i2c.c";
  req.lines = {30, 64};
  req.breakpoints = {{.line = 30}, {.line = 64}};
  auto response = client().send(req);

  // Read request and process it.
  context().OnStreamReadable();

  // Run client to receive response.
  RunClient();
  auto got = response.get();
  EXPECT_EQ(got.error, false);
  EXPECT_EQ(got.response.breakpoints.size(), req.breakpoints.value().size());
  EXPECT_EQ(got.response.breakpoints[0].line.value(), req.lines.value()[0]);
  EXPECT_EQ(got.response.breakpoints[1].line.value(), req.lines.value()[1]);
  EXPECT_EQ(got.response.breakpoints[0].source.value().name.value(), req.source.name.value());
}

TEST_F(RequestBreakpointTest, UpdateBreakpoints) {
  InitializeDebugging();

  // Send breakpoint request from the client.
  dap::SetBreakpointsRequest req = {};
  req.source.name = "i2c.c";
  req.source.path = "/fuchsia/src/devices/i2c/bin/i2c/i2c.c";
  req.lines = {30, 40, 50};
  req.breakpoints = {{.line = 30}, {.line = 40}, {.line = 50}};
  auto response = client().send(req);

  // Read request and process it.
  context().OnStreamReadable();

  // Run client to receive response.
  RunClient();
  auto got = response.get();
  EXPECT_EQ(got.error, false);
  EXPECT_EQ(got.response.breakpoints.size(), req.breakpoints.value().size());
  EXPECT_EQ(context().GetBreakpointsForSource(req.source.name.value())->size(),
            req.breakpoints.value().size());

  // Remove a breakpoint and send request again. Old breakpoints should be replaced with the new
  // ones for the source file.
  req.lines = {40, 50};
  req.breakpoints = {{.line = 40}, {.line = 50}};
  auto updated_response = client().send(req);

  context().OnStreamReadable();
  RunClient();
  got = updated_response.get();
  EXPECT_EQ(got.error, false);
  EXPECT_EQ(got.response.breakpoints.size(), req.breakpoints.value().size());
  EXPECT_EQ(got.response.breakpoints[0].line.value(), req.lines.value()[0]);
  EXPECT_EQ(got.response.breakpoints[1].line.value(), req.lines.value()[1]);
  EXPECT_EQ(context().GetBreakpointsForSource(req.source.name.value())->size(),
            req.breakpoints.value().size());
}

}  // namespace zxdb
