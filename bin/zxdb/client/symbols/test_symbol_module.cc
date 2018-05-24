// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/test_symbol_module.h"

#include "garnet/bin/zxdb/client/host_utils.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"

namespace zxdb {

const char TestSymbolModule::kMyFunctionName[] = "MyFunction";
const int TestSymbolModule::kMyFunctionLine = 72;
const char TestSymbolModule::kNamespaceFunctionName[] =
    "my_ns::NamespaceFunction";
const char TestSymbolModule::kMyMemberOneName[] = "my_ns::MyClass::MyMemberOne";
const char TestSymbolModule::kFunctionInTest2Name[] =
    "ClassInTest2::FunctionInTest2";
const char TestSymbolModule::kMyMemberTwoName[] =
    "my_ns::MyClass::Inner::MyMemberTwo";

TestSymbolModule::TestSymbolModule() = default;
TestSymbolModule::~TestSymbolModule() = default;

// static
std::string TestSymbolModule::GetTestFileName() {
  // We assume the "test_data:copy_test_so" build step has generated and
  // copied this shared library compiled for the target to the same directory
  // as the test.
  std::string path = GetSelfPath();
  size_t last_slash = path.rfind('/');
  if (last_slash == std::string::npos) {
    path = "./";  // Just hope the current directory works.
  } else {
    path.resize(last_slash + 1);
  }
  return path + "libzxdb_symbol_test.targetso";
}

bool TestSymbolModule::Load(std::string* err_msg) {
  std::string filename = GetTestFileName();

  llvm::Expected<llvm::object::OwningBinary<llvm::object::Binary>> bin_or_err =
      llvm::object::createBinary(filename);
  if (!bin_or_err) {
    auto err_str = llvm::toString(bin_or_err.takeError());
    *err_msg =
        "Error loading symbols for \"" + filename + "\", LLVM said: " + err_str;
    return false;
  }

  auto binary_pair = bin_or_err->takeBinary();
  binary_buffer_ = std::move(binary_pair.second);
  binary_ = std::move(binary_pair.first);

  llvm::object::ObjectFile* obj =
      static_cast<llvm::object::ObjectFile*>(binary_.get());
  context_ = llvm::DWARFContext::create(
      *obj, nullptr, llvm::DWARFContext::defaultErrorHandler);

  compile_units_.parse(*context_, context_->getDWARFObj().getInfoSection());
  return true;
}

}  // namespace zxdb
