// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/test_symbol_module.h"

#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "src/developer/debug/zxdb/common/host_util.h"
#include "src/developer/debug/zxdb/symbols/dwarf_binary_impl.h"

namespace zxdb {

const char TestSymbolModule::kMyNamespaceName[] = "my_ns";
const char TestSymbolModule::kMyFunctionName[] = "MyFunction";
const int TestSymbolModule::kMyFunctionLine = 109;
const uint64_t TestSymbolModule::kMyFunctionAddress = 0x1460u;
const size_t TestSymbolModule::kMyFunctionPrologueSize = 8;
const char TestSymbolModule::kNamespaceFunctionName[] = "my_ns::NamespaceFunction";
const char TestSymbolModule::kMyClassName[] = "my_ns::MyClass";
const char TestSymbolModule::kMyInnerClassName[] = "my_ns::MyClass::Inner";
const char TestSymbolModule::kMyMemberOneName[] = "my_ns::MyClass::MyMemberOne";
const char TestSymbolModule::kFunctionInTest2Name[] = "ClassInTest2::FunctionInTest2";
const char TestSymbolModule::kMyMemberTwoName[] = "my_ns::MyClass::Inner::MyMemberTwo";
const char TestSymbolModule::kAnonNSFunctionName[] = "AnonNSFunction";
const char TestSymbolModule::kGlobalName[] = "my_ns::kGlobal";
const char TestSymbolModule::kClassStaticName[] = "my_ns::MyClass::kClassStatic";
const char TestSymbolModule::kPltFunctionName[] = "__stack_chk_fail";
const uint64_t TestSymbolModule::kPltFunctionOffset = 0x1570;

namespace {

inline std::string GetTestFilePath(const std::string& rel_path) {
  std::string path = GetSelfPath();
  size_t last_slash = path.rfind('/');
  if (last_slash == std::string::npos) {
    path = "./";  // Just hope the current directory works.
  } else {
    path.resize(last_slash + 1);
  }
  return path + rel_path;
}

// The test files will be copied over to this specific location at build time.
const char kRelativeTestDataPath[] = "test_data/zxdb/";

}  // namespace

// static
std::string TestSymbolModule::GetTestDataDir() { return GetTestFilePath(kRelativeTestDataPath); }

// static
std::string TestSymbolModule::GetTestFileName() {
  return GetTestDataDir() + "libzxdb_symbol_test.targetso";
}

// static
std::string TestSymbolModule::GetCheckedInTestFileName() {
  return GetTestDataDir() + "libsymbol_test_so.targetso";
}

// static
std::string TestSymbolModule::GetCheckedInTestFileBuildID() { return "596f4c8afa5a0a43"; }

// static
std::string TestSymbolModule::GetStrippedCheckedInTestFileName() {
  return GetTestDataDir() + "libsymbol_test_so_stripped.targetso";
}

// static
Identifier TestSymbolModule::SplitName(std::string_view input) {
  const std::string separator("::");
  Identifier result;

  size_t input_index = 0;
  while (input_index < input.size()) {
    size_t next = input.find(separator, input_index);

    std::string cur_name;
    if (next == std::string::npos) {
      cur_name = input.substr(input_index);
      input_index = input.size();
    } else {
      cur_name = input.substr(input_index, next - input_index);
      input_index = next + separator.size();  // Skip over "::".
    }

    result.AppendComponent(IdentifierComponent(std::move(cur_name)));
  }
  return result;
}

TestSymbolModule::TestSymbolModule(Kind kind) {
  // This leaves the binary name empty since these files have both symbols and code together.
  switch (kind) {
    case kCheckedIn:
      sym_name_ = GetCheckedInTestFileName();
      break;
    case kBuilt:
      sym_name_ = GetTestFileName();
      break;
  }
}

TestSymbolModule::TestSymbolModule(const std::string& sym_name, const std::string& binary_name)
    : sym_name_(sym_name), binary_name_(binary_name) {}

TestSymbolModule::~TestSymbolModule() = default;

Err TestSymbolModule::Init(const std::string& build_dir, bool should_index) {
  auto binary = std::make_unique<DwarfBinaryImpl>(sym_name_, binary_name_, std::string());
  if (Err err = binary->Load(); err.has_error())
    return err;
  symbols_ = fxl::MakeRefCounted<ModuleSymbolsImpl>(std::move(binary), build_dir, should_index);
  return Err();
}

}  // namespace zxdb
