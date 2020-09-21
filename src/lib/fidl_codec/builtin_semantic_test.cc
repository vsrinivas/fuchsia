// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <gtest/gtest.h>

#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/semantic.h"
#include "src/lib/fidl_codec/semantic_parser_test.h"
#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fidl_codec/wire_types.h"

namespace fidl_codec {
namespace semantic {

constexpr uint64_t kPid = 0x1234;
constexpr uint64_t kTid = 0x4321;
constexpr uint32_t kHandle = 0x1111;
constexpr uint32_t kChannel0 = 0x1000;
constexpr uint32_t kChannel1 = 0x2000;
constexpr uint32_t kChannel2 = 0x3000;
constexpr uint32_t kChannel3 = 0x4000;

class BuiltinSemanticTest : public SemanticParserTest {
 public:
  BuiltinSemanticTest();

  void SetHandleSemantic(std::string_view type, std::string_view path) {
    handle_semantic_.AddInferredHandleInfo(kPid, kHandle, type, path, "");
  }

  void SetHandleSemantic(std::string_view type, int64_t fd) {
    handle_semantic_.AddInferredHandleInfo(kPid, kHandle, type, fd, "");
  }

  void ExecuteWrite(const MethodSemantic* method_semantic, const StructValue* request,
                    const StructValue* response);

  void ExecuteRead(const MethodSemantic* method_semantic, const StructValue* request,
                   const StructValue* response);
  void ShortDisplay(std::ostream& os, const MethodDisplay* display, const StructValue* request,
                    const StructValue* response);

 protected:
  HandleSemantic handle_semantic_;
  const zx_handle_info_t channel0_;
  const zx_handle_info_t channel2_;
};

BuiltinSemanticTest::BuiltinSemanticTest()
    : channel0_({kChannel0, 0, 0, 0}), channel2_({kChannel2, 0, 0, 0}) {
  library_loader_.ParseBuiltinSemantic();
  handle_semantic_.AddLinkedHandles(kPid, kChannel0, kChannel1);
  handle_semantic_.AddLinkedHandles(kPid, kChannel2, kChannel3);
}

void BuiltinSemanticTest::ExecuteWrite(const MethodSemantic* method_semantic,
                                       const StructValue* request, const StructValue* response) {
  fidl_codec::semantic::AssignmentSemanticContext context(&handle_semantic_, kPid, kTid, kHandle,
                                                          ContextType::kWrite, request, response);
  method_semantic->ExecuteAssignments(&context);
}

void BuiltinSemanticTest::ExecuteRead(const MethodSemantic* method_semantic,
                                      const StructValue* request, const StructValue* response) {
  fidl_codec::semantic::AssignmentSemanticContext context(&handle_semantic_, kPid, kTid, kHandle,
                                                          ContextType::kRead, request, response);
  method_semantic->ExecuteAssignments(&context);
}

void BuiltinSemanticTest::ShortDisplay(std::ostream& os, const MethodDisplay* display,
                                       const StructValue* request, const StructValue* response) {
  PrettyPrinter printer(os, WithoutColors, true, "", 100, false);
  fidl_codec::semantic::SemanticContext context(&handle_semantic_, kPid, ZX_HANDLE_INVALID, request,
                                                response);
  bool first_argument = true;
  for (const auto& expression : display->inputs()) {
    if (first_argument) {
      printer << '(';
      first_argument = false;
    } else {
      printer << ", ";
    }
    expression->PrettyPrint(printer, &context);
  }
  if (!first_argument) {
    printer << ')';
  }
  printer << '\n';
  bool first_result = true;
  for (const auto& expression : display->results()) {
    printer << (first_result ? "-> " : ", ");
    first_result = false;
    expression->PrettyPrint(printer, &context);
  }
  if (!first_result) {
    printer << '\n';
  }
}

// Check Node::Clone: request.object = handle
TEST_F(BuiltinSemanticTest, CloneWrite) {
  // Checks that Node::Clone exists in fuchsia.io.
  Library* library = library_loader_.GetLibraryFromName("fuchsia.io");
  ASSERT_NE(library, nullptr);
  library->DecodeTypes();
  Interface* interface = nullptr;
  library->GetInterfaceByName("fuchsia.io/Node", &interface);
  ASSERT_NE(interface, nullptr);
  InterfaceMethod* method = interface->GetMethodByName("Clone");
  ASSERT_NE(method, nullptr);
  // Checks that the builtin semantic is defined for Clone.
  ASSERT_NE(method->semantic(), nullptr);

  // Check that by writing on this handle:
  SetHandleSemantic("dir", "/svc");

  // This message (we only define the fields used by the semantic):
  StructValue request(*method->request());
  request.AddField("object", std::make_unique<HandleValue>(channel0_));

  ExecuteWrite(method->semantic(), &request, nullptr);

  // We have this handle semantic for kChannel1.
  const InferredHandleInfo* inferred_handle_info =
      handle_semantic_.GetInferredHandleInfo(kPid, kChannel1);
  ASSERT_NE(inferred_handle_info, nullptr);
  ASSERT_EQ(inferred_handle_info->type(), "dir");
  ASSERT_EQ(inferred_handle_info->path(), "/svc");
  ASSERT_EQ(inferred_handle_info->attributes(), "cloned");
}

// Check Node::Clone: request.object = handle
TEST_F(BuiltinSemanticTest, CloneRead) {
  // Checks that Node::Clone exists in fuchsia.io.
  Library* library = library_loader_.GetLibraryFromName("fuchsia.io");
  ASSERT_NE(library, nullptr);
  library->DecodeTypes();
  Interface* interface = nullptr;
  library->GetInterfaceByName("fuchsia.io/Node", &interface);
  ASSERT_NE(interface, nullptr);
  InterfaceMethod* method = interface->GetMethodByName("Clone");
  ASSERT_NE(method, nullptr);
  // Checks that the builtin semantic is defined for Clone.
  ASSERT_NE(method->semantic(), nullptr);

  // Check that by writing on this handle:
  SetHandleSemantic("dir", "/svc");

  // This message (we only define the fields used by the semantic):
  StructValue request(*method->request());
  request.AddField("object", std::make_unique<HandleValue>(channel0_));

  ExecuteRead(method->semantic(), &request, nullptr);

  // We have this handle semantic for kChannel1.
  const InferredHandleInfo* inferred_handle_info =
      handle_semantic_.GetInferredHandleInfo(kPid, kChannel0);
  ASSERT_NE(inferred_handle_info, nullptr);
  ASSERT_EQ(inferred_handle_info->type(), "dir");
  ASSERT_EQ(inferred_handle_info->path(), "/svc");
  ASSERT_EQ(inferred_handle_info->attributes(), "cloned");
}

// Check Node::Clone: request.object = handle
TEST_F(BuiltinSemanticTest, CloneFd) {
  // Checks that Node::Clone exists in fuchsia.io.
  Library* library = library_loader_.GetLibraryFromName("fuchsia.io");
  ASSERT_NE(library, nullptr);
  library->DecodeTypes();
  Interface* interface = nullptr;
  library->GetInterfaceByName("fuchsia.io/Node", &interface);
  ASSERT_NE(interface, nullptr);
  InterfaceMethod* method = interface->GetMethodByName("Clone");
  ASSERT_NE(method, nullptr);
  // Checks that the builtin semantic is defined for Clone.
  ASSERT_NE(method->semantic(), nullptr);

  // Check that by writing on this handle:
  SetHandleSemantic("handle", 2);

  // This message (we only define the fields used by the semantic):
  StructValue request(*method->request());
  request.AddField("object", std::make_unique<HandleValue>(channel0_));

  ExecuteRead(method->semantic(), &request, nullptr);

  // We have this handle semantic for kChannel1.
  const InferredHandleInfo* inferred_handle_info =
      handle_semantic_.GetInferredHandleInfo(kPid, kChannel0);
  ASSERT_NE(inferred_handle_info, nullptr);
  ASSERT_EQ(inferred_handle_info->type(), "handle");
  ASSERT_EQ(inferred_handle_info->attributes(), "cloned");
  ASSERT_EQ(inferred_handle_info->fd(), 2);
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
  const InferredHandleInfo* inferred_handle_info =
      handle_semantic_.GetInferredHandleInfo(kPid, kChannel1);
  ASSERT_NE(inferred_handle_info, nullptr);
  ASSERT_EQ(inferred_handle_info->type(), "dir");
  ASSERT_EQ(inferred_handle_info->path(), "/svc/fuchsia.sys.Launcher");
}

// Check Launcher::CreateComponent.
TEST_F(BuiltinSemanticTest, CreateComponent) {
  // Checks that Launcher::CreateComponent exists in fuchsia.sys.
  Library* library = library_loader_.GetLibraryFromName("fuchsia.sys");
  ASSERT_NE(library, nullptr);
  library->DecodeTypes();
  Interface* interface = nullptr;
  library->GetInterfaceByName("fuchsia.sys/Launcher", &interface);
  ASSERT_NE(interface, nullptr);
  InterfaceMethod* method = interface->GetMethodByName("CreateComponent");
  ASSERT_NE(method, nullptr);
  // Checks that the builtin semantic is defined for CreateComponent.
  ASSERT_NE(method->semantic(), nullptr);

  // Check that by writing on this handle:
  SetHandleSemantic("dir", "/svc/fuchsia.sys.Launcher");

  // This message (we only define the fields used by the semantic):
  StructValue request(*method->request());
  auto launch_info = std::make_unique<StructValue>(
      method->request()->SearchMember("launch_info")->type()->AsStructType()->struct_definition());
  launch_info->AddField("url",
                        std::make_unique<StringValue>(
                            "fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx"));
  launch_info->AddField("directory_request", std::make_unique<HandleValue>(channel0_));
  request.AddField("launch_info", std::move(launch_info));
  request.AddField("controller", std::make_unique<HandleValue>(channel2_));

  ExecuteWrite(method->semantic(), &request, nullptr);

  // We have these handle semantics for kChannel1 and kChannel3.
  const InferredHandleInfo* inferred_handle_info_1 =
      handle_semantic_.GetInferredHandleInfo(kPid, kChannel1);
  ASSERT_NE(inferred_handle_info_1, nullptr);
  ASSERT_EQ(inferred_handle_info_1->type(), "server");
  ASSERT_EQ(inferred_handle_info_1->path(),
            "fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx");
  const InferredHandleInfo* inferred_handle_info_2 =
      handle_semantic_.GetInferredHandleInfo(kPid, kChannel3);
  ASSERT_NE(inferred_handle_info_2, nullptr);
  ASSERT_EQ(inferred_handle_info_2->type(), "server-control");
  ASSERT_EQ(inferred_handle_info_2->path(),
            "fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx");
}

// Check short display of Directory::Open.
TEST_F(BuiltinSemanticTest, OpenShortDisplay) {
  // Checks that Directory::Open exists in fuchsia.io.
  Library* library = library_loader_.GetLibraryFromName("fuchsia.io");
  ASSERT_NE(library, nullptr);
  library->DecodeTypes();
  Interface* interface = nullptr;
  library->GetInterfaceByName("fuchsia.io/Directory", &interface);
  ASSERT_NE(interface, nullptr);
  InterfaceMethod* method = interface->GetMethodByName("Open");
  ASSERT_NE(method, nullptr);
  // Checks that the short display is defined for Open.
  ASSERT_NE(method->short_display(), nullptr);

  // This message (we only define the fields used by the display):
  StructValue request(*method->request());
  request.AddField("path", std::make_unique<StringValue>("fuchsia.sys.Launcher"));
  request.AddField("object", std::make_unique<HandleValue>(channel0_));

  std::stringstream os;
  ShortDisplay(os, method->short_display(), &request, nullptr);
  ASSERT_EQ(os.str(),
            "(\"fuchsia.sys.Launcher\")\n"
            "-> 00002000\n");
}

// Check short display of File::Seek.
TEST_F(BuiltinSemanticTest, FileSeekShortDisplay) {
  // Checks that Node::Clone exists in fuchsia.io.
  Library* library = library_loader_.GetLibraryFromName("fuchsia.io");
  ASSERT_NE(library, nullptr);
  library->DecodeTypes();
  Interface* interface = nullptr;
  library->GetInterfaceByName("fuchsia.io/File", &interface);
  ASSERT_NE(interface, nullptr);
  InterfaceMethod* method = interface->GetMethodByName("Seek");
  ASSERT_NE(method, nullptr);
  // Checks that the short display is defined for Seek.
  ASSERT_NE(method->short_display(), nullptr);

  // This message (we only define the fields used by the display):
  StructValue request(*method->request());
  request.AddField("start", std::make_unique<IntegerValue>(0, false));
  request.AddField("offset", std::make_unique<IntegerValue>(1000, false));

  std::stringstream os;
  ShortDisplay(os, method->short_display(), &request, nullptr);
  ASSERT_EQ(os.str(), "(START, 1000)\n");
}

// Check short display of File::Write.
TEST_F(BuiltinSemanticTest, FileWriteShortDisplay) {
  // Checks that Node::Clone exists in fuchsia.io.
  Library* library = library_loader_.GetLibraryFromName("fuchsia.io");
  ASSERT_NE(library, nullptr);
  library->DecodeTypes();
  Interface* interface = nullptr;
  library->GetInterfaceByName("fuchsia.io/File", &interface);
  ASSERT_NE(interface, nullptr);
  InterfaceMethod* method = interface->GetMethodByName("Write");
  ASSERT_NE(method, nullptr);
  // Checks that the short display is defined for Write.
  ASSERT_NE(method->short_display(), nullptr);

  // This message (we only define the fields used by the display):
  StructValue request(*method->request());
  auto vector = std::make_unique<VectorValue>();
  vector->AddValue(std::make_unique<IntegerValue>(10, false));
  vector->AddValue(std::make_unique<IntegerValue>(20, false));
  vector->AddValue(std::make_unique<IntegerValue>(30, false));
  vector->AddValue(std::make_unique<IntegerValue>(40, false));
  vector->AddValue(std::make_unique<IntegerValue>(50, false));
  request.AddField("data", std::move(vector));

  std::stringstream os;
  ShortDisplay(os, method->short_display(), &request, nullptr);
  ASSERT_EQ(os.str(), "(5 bytes)\n");
}

}  // namespace semantic
}  // namespace fidl_codec
