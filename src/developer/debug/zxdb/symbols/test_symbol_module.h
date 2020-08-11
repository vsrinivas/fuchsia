// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_TEST_SYMBOL_MODULE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_TEST_SYMBOL_MODULE_H_

#include <memory>
#include <string>
#include <string_view>

#include "llvm/DebugInfo/DWARF/DWARFCompileUnit.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Object/ObjectFile.h"
#include "src/developer/debug/zxdb/symbols/identifier.h"
#include "src/developer/debug/zxdb/symbols/module_symbols_impl.h"
#include "src/lib/fxl/macros.h"

namespace llvm {

class DWARFCompileUnit;
class DWARFContext;
class MemoryBuffer;

namespace object {
class Binary;
}  // namespace object

}  // namespace llvm

namespace zxdb {

// This class loads the unstripped zxdb_symbol_test module with the required LLDB classes for
// writing symbol testing.
class TestSymbolModule {
 public:
  // Which of the symbol files to load int he constructor.
  enum Kind {
    kCheckedIn,  // The stable checked-in binary. See GetCheckedInTestFileName().
    kBuilt       // The one built in the current build. See GetTestFileName().
  };

  // These constants identify locations in the symbol test files.
  static const char kMyNamespaceName[];
  static const char kMyFunctionName[];
  static const int kMyFunctionLine;
  static const uint64_t kMyFunctionAddress;
  static const size_t kMyFunctionPrologueSize;
  static const char kNamespaceFunctionName[];
  static const char kMyClassName[];
  static const char kMyInnerClassName[];
  static const char kMyMemberOneName[];
  static const char kFunctionInTest2Name[];
  static const char kMyMemberTwoName[];
  static const char kAnonNSFunctionName[];
  static const char kGlobalName[];
  static const char kClassStaticName[];
  static const char kPltFunctionName[];
  static const uint64_t kPltFunctionOffset;

  // Returns the relative directory where the test program can find the checked-in test files. It
  // will have a trailing slash.
  static std::string GetTestDataDir();

  // Returns the name of the .so file used by this class for doing tests with it that involve
  // different types of setup.
  static std::string GetTestFileName();

  // Returns the checked in .so used for line testing. As the mapping changes between architectures,
  // the file is compiled offline and remains the same.
  static std::string GetCheckedInTestFileName();

  // Returns the Build ID for the checked in .so returned by GetCheckedInTestFileName.
  static std::string GetCheckedInTestFileBuildID();

  // Returns a stripped version of the file returned by GetCheckedInTestFileName().
  static std::string GetStrippedCheckedInTestFileName();

  // Helper to convert symbol names to vectors of components without using the "expr" library. This
  // just splits on "::" which handles most cases but not elaborate templates.
  static Identifier SplitName(std::string_view input);

  // You must call Init() after the constructor to actually load the file.
  TestSymbolModule(Kind);
  TestSymbolModule(const std::string& sym_name, const std::string& binary_name);

  ~TestSymbolModule();

  Err Init(const std::string& build_dir = "", bool should_index = true);

  ModuleSymbolsImpl* symbols() const { return symbols_.get(); }

 private:
  std::string sym_name_;
  std::string binary_name_;

  fxl::RefPtr<ModuleSymbolsImpl> symbols_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestSymbolModule);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_TEST_SYMBOL_MODULE_H_
