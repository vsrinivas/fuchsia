// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/process_symbol_data_provider.h"

#include <gtest/gtest.h>

#include "llvm/BinaryFormat/Dwarf.h"
#include "src/developer/debug/zxdb/client/mock_process.h"
#include "src/developer/debug/zxdb/client/mock_target.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"

namespace zxdb {

class ProcessSymbolDataProviderTest : public TestWithLoop {
 public:
  ProcessSymbolDataProviderTest() {}

 protected:
  ProcessSymbolsTestSetup symbols_;
};

TEST_F(ProcessSymbolDataProviderTest, GetTLSSegment) {
  const uint64_t kLoadAddress = 0xf00db4b3;
  Session session;
  MockTarget target(&session);
  MockProcess process(&target);

  fxl::RefPtr<ProcessSymbolDataProvider> provider =
      fxl::MakeRefCounted<ProcessSymbolDataProvider>(process.GetWeakPtr());
  SymbolContext symbol_context(kLoadAddress);
  fxl::RefPtr<MockModuleSymbols> module_syms = fxl::MakeRefCounted<MockModuleSymbols>("foo.so");

  symbols_.InjectModule("foo", "abcdef012345", kLoadAddress, module_syms);
  process.set_symbols(&symbols_.process());

  std::vector<uint8_t> program{
      llvm::dwarf::DW_OP_const8u, 0, 1, 2, 3, 4, 5, 6, 7, llvm::dwarf::DW_OP_form_tls_address,
  };

  std::vector<uint8_t> thrd_t_helper{
      llvm::dwarf::DW_OP_lo_user + 1,  // Invalid opcode because this one shouldn't be used.
  };

  std::vector<uint8_t> link_map_tls_modid_helper{
      llvm::dwarf::DW_OP_const8u, 7, 6, 5, 4, 3, 2, 1, 0, llvm::dwarf::DW_OP_plus,
  };

  std::vector<uint8_t> tlsbase_helper{
      llvm::dwarf::DW_OP_const8u, 1, 0, 1, 0, 1, 0, 1, 0, llvm::dwarf::DW_OP_plus,
  };

  Process::TLSHelpers helpers = {
      .thrd_t = std::move(thrd_t_helper),
      .link_map_tls_modid = std::move(link_map_tls_modid_helper),
      .tlsbase = std::move(tlsbase_helper),
  };

  process.set_tls_helpers(std::move(helpers));

  ErrOr<uint64_t> got(0);
  bool ran_callback = false;

  provider->GetTLSSegment(symbol_context, [&got, &ran_callback](ErrOr<uint64_t> result) {
    got = result;
    ran_callback = true;
  });

  loop().RunUntilNoTasks();

  ASSERT_TRUE(ran_callback);
  ASSERT_FALSE(got.has_error()) << (got.has_error() ? got.err().msg() : "");
  EXPECT_EQ(0x2020404060608u, got.value());
}

}  // namespace zxdb
