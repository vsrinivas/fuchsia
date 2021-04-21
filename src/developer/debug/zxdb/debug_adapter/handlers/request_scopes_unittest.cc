// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_scopes_unittest.h"

#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/common/scoped_temp_file.h"
#include "src/developer/debug/zxdb/symbols/function.h"

namespace zxdb {

TEST_F(RequestScopesTest, Success) {
  // Get stackframe.
  constexpr uint64_t kStack = 0x7890;
  // ScopedTempFile temp_file;
  fxl::RefPtr<Function> function(fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram));
  function->set_assigned_name("test_func1");
  function->set_code_ranges(AddressRanges(AddressRange(0x10000, 0x10020)));
  auto location = Location(0x10010, FileLine("test_file.cc", 23), 10,
                           SymbolContext::ForRelativeAddresses(), function);
  std::vector<std::unique_ptr<Frame>> frames;
  frames.push_back(std::make_unique<MockFrame>(&session(), thread(), location, kStack));

  dap::ResponseOrError<dap::StackTraceResponse> stack_response;
  ASSERT_NO_FATAL_FAILURE(stack_response = GetStackTrace(std::move(frames)););
  ASSERT_FALSE(stack_response.error);

  // Get frame ID from stacktrace.
  dap::integer frame_id = 0;
  ASSERT_FALSE(stack_response.response.stackFrames.empty());
  frame_id = stack_response.response.stackFrames[0].id;
  ASSERT_NE(frame_id, 0);

  // Send scopes request from the client.
  dap::ScopesRequest request;
  request.frameId = frame_id;
  auto response = client().send(request);

  // Read request and process it in server.
  context().OnStreamReadable();
  loop().RunUntilNoTasks();

  // Run client to receive response.
  RunClient();
  auto got = response.get();
  ASSERT_FALSE(got.error);
  ASSERT_EQ(got.response.scopes.size(), 3u);
  EXPECT_EQ(got.response.scopes[0].name, "Locals");
  EXPECT_EQ(got.response.scopes[1].name, "Arguments");
  EXPECT_EQ(got.response.scopes[2].name, "Registers");
  // Check that all scopes reported variables reference (0 - invalid).
  EXPECT_NE(got.response.scopes[0].variablesReference, 0);
  EXPECT_NE(got.response.scopes[1].variablesReference, 0);
  EXPECT_NE(got.response.scopes[2].variablesReference, 0);
}

TEST_F(RequestScopesTest, NoSymbolError) {
  // Inject exception with no symbol information.
  constexpr uint64_t kStack = 0x7890;
  std::vector<std::unique_ptr<Frame>> frames;
  frames.push_back(std::make_unique<MockFrame>(&session(), thread(), Location(), kStack));
  dap::ResponseOrError<dap::StackTraceResponse> stack_response;
  ASSERT_NO_FATAL_FAILURE(stack_response = GetStackTrace(std::move(frames)););
  ASSERT_FALSE(stack_response.error);

  // Get frame ID from stacktrace.
  dap::integer frame_id = 0;
  ASSERT_FALSE(stack_response.response.stackFrames.empty());
  frame_id = stack_response.response.stackFrames[0].id;
  ASSERT_NE(frame_id, 0);

  // Send scopes request from the client.
  dap::ScopesRequest request = {};
  request.frameId = frame_id;
  auto response = client().send(request);

  // Read request and process it in server.
  context().OnStreamReadable();
  loop().RunUntilNoTasks();

  // Run client to receive response.
  RunClient();
  auto got = response.get();
  EXPECT_TRUE(got.error);
}

}  // namespace zxdb
