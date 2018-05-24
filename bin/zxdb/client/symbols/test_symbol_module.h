// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>

#include "garnet/public/lib/fxl/macros.h"
#include "llvm/DebugInfo/DWARF/DWARFCompileUnit.h"

namespace llvm {

class DWARFCompileUnit;
class DWARFContext;
class MemoryBuffer;

namespace object {
class Binary;
} // namespace object

}  // namespace llvm

namespace zxdb {

// This class loads the unstripped zxdb_symbol_test module with the
// required LLDB classes for writing symbol testing.
class TestSymbolModule {
 public:
  // These constants identify locations in the symbol test files.
  static const char kMyFunctionName[];
  static const int kMyFunctionLine;
  static const char kNamespaceFunctionName[];
  static const char kMyMemberOneName[];
  static const char kFunctionInTest2Name[];
  static const char kMyMemberTwoName[];

  TestSymbolModule();
  ~TestSymbolModule();

  // Returns the name of the .so file used by this class for doing tests with
  // it that involve different types of setup.
  static std::string GetTestFileName();

  // Loads the test file. On failure, returns false and sets the given error
  // message to be something helpful.
  bool Load(std::string* err_msg);

  llvm::DWARFContext* context() { return context_.get(); }
  llvm::DWARFUnitSection<llvm::DWARFCompileUnit>& compile_units() {
    return compile_units_;
  }

 private:
  std::unique_ptr<llvm::MemoryBuffer> binary_buffer_;  // Backing for binary_.
  std::unique_ptr<llvm::object::Binary> binary_;
  std::unique_ptr<llvm::DWARFContext> context_;

  llvm::DWARFUnitSection<llvm::DWARFCompileUnit> compile_units_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestSymbolModule);
};

}  // namespace zxdb
