// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_variables_unittest.h"

#include <dap/protocol.h>
#include <llvm/BinaryFormat/Dwarf.h>

#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/common/scoped_temp_file.h"
#include "src/developer/debug/zxdb/debug_adapter/context_test.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/variable_test_support.h"
namespace zxdb {

TEST_F(RequestVariablesTest, LocalsNoChildren) {
  // Make a mock frame with test_var variable.
  auto variable = MakeUint64VariableForTest(
      "test_var", 0x10000, 0x10020,
      DwarfExpr({llvm::dwarf::DW_OP_reg0, llvm::dwarf::DW_OP_stack_value}));

  fxl::RefPtr<Function> function(fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram));
  function->set_assigned_name("test_func1");
  function->set_code_ranges(AddressRanges(AddressRange(0x10000, 0x10020)));
  function->set_variables({LazySymbol(std::move(variable))});

  ScopedTempFile temp_file;
  auto location = Location(0x10010, FileLine(temp_file.name(), 23), 10,
                           SymbolContext::ForRelativeAddresses(), function);

  std::vector<std::unique_ptr<Frame>> frames;
  constexpr uint64_t kStack = 0x7890;
  frames.push_back(std::make_unique<MockFrame>(&session(), thread(), location, kStack));

  // Get scopes response.
  dap::ResponseOrError<dap::ScopesResponse> scopes_response;
  ASSERT_NO_FATAL_FAILURE(scopes_response = GetScopesResponse(std::move(frames)););
  ASSERT_FALSE(scopes_response.error);

  // Send request from the client.
  dap::VariablesRequest request;
  request.variablesReference = scopes_response.response.scopes[0].variablesReference;
  auto response = client().send(request);

  // Read request and process it in server.
  context().OnStreamReadable();
  loop().RunUntilNoTasks();

  // Run client to receive response.
  RunClient();
  auto got = response.get();
  EXPECT_FALSE(got.error);
  ASSERT_EQ(got.response.variables.size(), 1u);
  EXPECT_EQ(got.response.variables[0].name, "test_var");
}

}  // namespace zxdb
