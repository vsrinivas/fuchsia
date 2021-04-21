// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_VARIABLES_UNITTEST_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_VARIABLES_UNITTEST_H_

#include <type_traits>

#include <dap/protocol.h>
#include <dap/session.h>
#include <dap/types.h>
#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/debug_adapter/context_test.h"
#include "src/developer/debug/zxdb/debug_adapter/handlers/request_scopes_unittest.h"

namespace zxdb {

class RequestVariablesTest : public RequestScopesTest {
 public:
  dap::ResponseOrError<dap::ScopesResponse> GetScopesResponse(
      std::vector<std::unique_ptr<Frame>> frames) {
    auto stack_response = GetStackTrace(std::move(frames));
    EXPECT_FALSE(stack_response.error);
    EXPECT_FALSE(stack_response.response.stackFrames.empty());

    if (stack_response.response.stackFrames.empty()) {
      return {};
    }

    dap::integer frame_id = 0;
    frame_id = stack_response.response.stackFrames[0].id;
    EXPECT_NE(frame_id, 0);

    // Send scopes request from the client.
    dap::ScopesRequest request = {};
    request.frameId = frame_id;
    auto response = client().send(request);

    // Read request and process it in server.
    context().OnStreamReadable();
    loop().RunUntilNoTasks();

    // Run client to receive response.
    RunClient();
    return response.get();
  }
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_VARIABLES_UNITTEST_H_
