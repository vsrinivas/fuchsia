// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_attach.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/debug_adapter/context_test.h"

namespace zxdb {

namespace {

class RequestAttachTest : public DebugAdapterContextTest {};

}  // namespace

TEST_F(RequestAttachTest, AttachProcessName) {
  InitializeDebugging();

  // Send attach request from the client.
  dap::AttachRequestZxdb req = {};
  req.process = "test";
  auto response = client().send(req);

  // Read request and process it.
  context().OnStreamReadable();

  // Run client to receive response.
  RunClient();
  auto got = response.get();
  EXPECT_FALSE(got.error);
}

}  // namespace zxdb
