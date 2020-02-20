// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "gtest/gtest.h"
#include "library_loader.h"
#include "src/lib/fidl_codec/semantic.h"
#include "src/lib/fidl_codec/semantic_parser_test.h"
#include "src/lib/fidl_codec/wire_object.h"

namespace fidl_codec {
namespace semantic {

constexpr uint64_t kPid = 0x1234;
constexpr uint32_t kHandle = 0x1111;
constexpr uint32_t kChannel0 = 0x1000;
constexpr uint32_t kChannel1 = 0x2000;

class BuiltinSemanticTest : public SemanticParserTest {
 public:
  BuiltinSemanticTest();

  void SetHandleSemantic(std::string_view type, std::string_view path) {
    handle_semantic_.AddHandleDescription(kPid, kHandle, type, path);
  }

  void ExecuteWrite(const MethodSemantic* method_semantic, const StructValue* request,
                    const StructValue* response);

 protected:
  HandleSemantic handle_semantic_;
  const zx_handle_info_t channel0_;
};

BuiltinSemanticTest::BuiltinSemanticTest() : channel0_({kChannel0, 0, 0, 0}) {
  library_loader_.ParseBuiltinSemantic();
  handle_semantic_.AddLinkedHandles(kPid, kChannel0, kChannel1);
}

void BuiltinSemanticTest::ExecuteWrite(const MethodSemantic* method_semantic,
                                       const StructValue* request, const StructValue* response) {
  fidl_codec::semantic::SemanticContext context(&handle_semantic_, kPid, kHandle,
                                                ContextType::kWrite, request, response);
  method_semantic->ExecuteAssignments(&context);
}

// Check Directory::Open: request.object = handle / request.path
TEST_F(BuiltinSemanticTest, Open) {
  // Checks that Directory::Open exists in fuchsia.io.
  Library* library = library_loader_.GetLibraryFromName("fuchsia.io");
  ASSERT_NE(library, nullptr);
  library->DecodeTypes();
  Interface* interface = nullptr;
  library->GetInterfaceByName("fuchsia.io/Directory", &interface);
  ASSERT_NE(interface, nullptr);
  InterfaceMethod* method = interface->GetMethodByName("Open");
  ASSERT_NE(method, nullptr);
  // Checks that the builtin semantic is defined for Open.
  ASSERT_NE(method->semantic(), nullptr);

  // Check that by writing on this handle:
  SetHandleSemantic("dir", "/svc");

  // This message (we only define the fields used by the semantic):
  StructValue request(*method->request());
  request.AddField("path", std::make_unique<StringValue>("fuchsia.sys.Launcher"));
  request.AddField("object", std::make_unique<HandleValue>(channel0_));

  ExecuteWrite(method->semantic(), &request, nullptr);

  // We have this handle semantic for kChannel1.
  const HandleDescription* description = handle_semantic_.GetHandleDescription(kPid, kChannel1);
  ASSERT_NE(description, nullptr);
  ASSERT_EQ(description->type(), "dir");
  ASSERT_EQ(description->path(), "/svc/fuchsia.sys.Launcher");
}

}  // namespace semantic
}  // namespace fidl_codec
