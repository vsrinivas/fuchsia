// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/library_loader.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/lib/fidl_codec/library_loader_test_data.h"
#include "src/lib/fidl_codec/list_test_data.h"

namespace fidl_codec {

// Check that we can load all the FIDL description files without errors (and without crash).
TEST(LibraryLoader, CheckAll) {
  fidl_codec_test::SdkExamples sdk_examples;
  fidl_codec_test::FidlcodecExamples other_examples;

  LibraryReadError err;
  LibraryLoader library_loader;
  // Test all the files in sdk/core.fidl_json.txt.
  for (const auto& element : sdk_examples.map()) {
    library_loader.AddContent(element.second, &err);
    ASSERT_EQ(err.value, LibraryReadError::kOk);
  }
  // Test all the fidl_codec files.
  for (const auto& element : other_examples.map()) {
    library_loader.AddContent(element.second, &err);
    ASSERT_EQ(err.value, LibraryReadError::kOk);
  }
  // Do the tests.
  ASSERT_TRUE(library_loader.DecodeAll());
}

TEST(LibraryLoader, LoadSimple) {
  fidl_codec_test::FidlcodecExamples examples;
  LibraryReadError err;
  LibraryLoader loader;
  for (const auto& element : examples.map()) {
    loader.AddContent(element.second, &err);
  }
  ASSERT_EQ(LibraryReadError::kOk, err.value);

  Library* library_ptr = loader.GetLibraryFromName("fidl.test.frobinator");
  ASSERT_NE(library_ptr, nullptr);

  std::string kDesiredInterfaceName = "fidl.test.frobinator/Frobinator";
  Interface* found_interface = nullptr;
  ASSERT_TRUE(library_ptr->GetInterfaceByName(kDesiredInterfaceName, &found_interface));

  ASSERT_NE(found_interface, nullptr) << "Could not find interface " << kDesiredInterfaceName;

  std::string kDesiredFullMethodName = "fidl.test.frobinator/Frobinator.Frob";
  const InterfaceMethod* found_method = nullptr;
  found_interface->GetMethodByFullName(kDesiredFullMethodName, &found_method);

  ASSERT_NE(found_method, nullptr) << "Could not find method " << kDesiredFullMethodName;
}

// Makes sure that loading works when you load one IR at a time, instead of in a bunch.
TEST(LibraryLoader, LoadSimpleOneAtATime) {
  fidl_codec_test::FidlcodecExamples examples;
  LibraryLoader loader;
  LibraryReadError err;
  for (const auto& element : examples.map()) {
    loader.AddContent(element.second, &err);
    ASSERT_EQ(LibraryReadError::kOk, err.value);
  }

  Library* library_ptr = loader.GetLibraryFromName("fidl.test.frobinator");
  ASSERT_NE(library_ptr, nullptr);

  std::string kDesiredInterfaceName = "fidl.test.frobinator/Frobinator";
  Interface* found_interface = nullptr;
  ASSERT_TRUE(library_ptr->GetInterfaceByName(kDesiredInterfaceName, &found_interface));

  ASSERT_NE(found_interface, nullptr) << "Could not find interface " << kDesiredInterfaceName;

  std::string kDesiredFullMethodName = "fidl.test.frobinator/Frobinator.Frob";
  const InterfaceMethod* found_method = nullptr;
  found_interface->GetMethodByFullName(kDesiredFullMethodName, &found_method);

  ASSERT_NE(found_method, nullptr) << "Could not find method " << kDesiredFullMethodName;
}

// Ensure that, if you load two libraries with the same name, the first one loaded wins.
// LoadAll calls AddContent using the last item in the list. That means that, for LoadAll, the
// last one wins.
TEST(LibraryLoader, FirstContentWins) {
  fidl_codec_test::FidlcodecExamples examples;
  std::string frobinator_value;
  const std::string file_to_replace = "frobinator.fidl.json";
  for (const auto& element : examples.map()) {
    if (element.first.compare(element.first.length() - file_to_replace.length(),
                              file_to_replace.length(), file_to_replace) == 0) {
      frobinator_value = element.second;
    }
  }
  ASSERT_NE(frobinator_value, "") << "Frobinator library not found";

  // Duplicate the frobinator entry, replacing the Frob method with a Frog method
  const std::string old_method = "\"Frob\"";
  const std::string new_method = "\"Frog\"";
  size_t pos = frobinator_value.find(old_method);
  while (pos != std::string::npos) {
    frobinator_value.replace(pos, old_method.size(), new_method);
    pos = frobinator_value.find(old_method, pos + new_method.size());
  }

  // Do the same for the underlying FrogRequest type
  const std::string old_payload_type_name = "\"fidl.test.frobinator/FrobinatorFrobRequest\"";
  const std::string new_payload_type_name = "\"fidl.test.frobinator/FrobinatorFrogRequest\"";
  pos = frobinator_value.find(old_payload_type_name);
  while (pos != std::string::npos) {
    frobinator_value.replace(pos, old_payload_type_name.size(), new_payload_type_name);
    pos = frobinator_value.find(old_payload_type_name, pos + new_payload_type_name.size());
  }

  LibraryReadError err;
  LibraryLoader loader;

  // Add the modified version. It will forbid the initial version to load.
  loader.AddContent(frobinator_value, &err);

  // Adds all the standard versions.
  for (const auto& element : examples.map()) {
    loader.AddContent(element.second, &err);
    ASSERT_EQ(LibraryReadError::kOk, err.value);
  }

  Library* library_ptr = loader.GetLibraryFromName("fidl.test.frobinator");
  ASSERT_NE(library_ptr, nullptr);

  std::string kDesiredInterfaceName = "fidl.test.frobinator/Frobinator";
  Interface* found_interface = nullptr;
  ASSERT_TRUE(library_ptr->GetInterfaceByName(kDesiredInterfaceName, &found_interface));

  ASSERT_NE(found_interface, nullptr) << "Could not find interface " << kDesiredInterfaceName;

  // We should find Frog, and not Frob.
  std::string kReplacedFullMethodName = "fidl.test.frobinator/Frobinator.Frob";
  const InterfaceMethod* not_found_method = nullptr;
  found_interface->GetMethodByFullName(kReplacedFullMethodName, &not_found_method);

  ASSERT_EQ(not_found_method, nullptr) << "Found replaced method " << kReplacedFullMethodName;

  std::string kDesiredFullMethodName = "fidl.test.frobinator/Frobinator.Frog";
  const InterfaceMethod* found_method = nullptr;
  found_interface->GetMethodByFullName(kDesiredFullMethodName, &found_method);

  ASSERT_NE(found_method, nullptr) << "Could not find method " << kDesiredFullMethodName;
  EXPECT_EQ("struct fidl.test.frobinator/FrobinatorFrogRequest {\n  string value;\n}",
            found_method->request()->ToString());
  EXPECT_EQ(nullptr, found_method->response());
}

TEST(LibraryLoader, InspectTypes) {
  LibraryReadError err;
  LibraryLoader loader;
  fidl_codec_test::FidlcodecExamples examples;
  for (const auto& element : examples.map()) {
    loader.AddContent(element.second, &err);
    ASSERT_EQ(LibraryReadError::kOk, err.value);
  }

  Library* library_ptr = loader.GetLibraryFromName("test.fidlcodec.examples");
  ASSERT_NE(library_ptr, nullptr);

  std::string kDesiredInterfaceName = "test.fidlcodec.examples/FidlCodecTestInterface";
  Interface* found_interface = nullptr;
  ASSERT_TRUE(library_ptr->GetInterfaceByName(kDesiredInterfaceName, &found_interface));

  const InterfaceMethod* found_method = nullptr;
  found_interface->GetMethodByFullName(
      "test.fidlcodec.examples/FidlCodecTestInterface.NullableXUnion", &found_method);

  ASSERT_NE(nullptr, found_method);
  ASSERT_NE(nullptr, found_method->request());
  EXPECT_EQ(
      "struct test.fidlcodec.examples/FidlCodecTestInterfaceNullableXUnionRequest {\n"
      "  union test.fidlcodec.examples/IntStructXunion {\n"
      "    1: int32 variant_i;\n"
      "    2: struct test.fidlcodec.examples/TwoStringStruct {\n"
      "      string value1;\n"
      "      string value2;\n"
      "    } variant_tss;\n"
      "  } isu;\n"
      "  int32 i;\n"
      "}",
      found_method->request()->ToString(true));
  EXPECT_EQ(
      "struct test.fidlcodec.examples/FidlCodecTestInterfaceNullableXUnionRequest {\n"
      "  union test.fidlcodec.examples/IntStructXunion isu;\n"
      "  int32 i;\n"
      "}",
      found_method->request()->ToString(false));

  found_method = nullptr;
  found_interface->GetMethodByFullName(
      "test.fidlcodec.examples/FidlCodecTestInterface.I64BitsMessage", &found_method);

  ASSERT_NE(nullptr, found_method);
  ASSERT_NE(nullptr, found_method->request());
  EXPECT_EQ(
      "struct test.fidlcodec.examples/FidlCodecTestInterfaceI64BitsMessageRequest {\n"
      "  bits test.fidlcodec.examples/I64Bits {\n"
      "    A = 4294967296;\n"
      "    B = 8589934592;\n"
      "    C = 17179869184;\n"
      "    D = 34359738368;\n"
      "  } v;\n"
      "}",
      found_method->request()->ToString(true));

  found_method = nullptr;
  found_interface->GetMethodByFullName("test.fidlcodec.examples/FidlCodecTestInterface.Table",
                                       &found_method);

  ASSERT_NE(nullptr, found_method);
  ASSERT_NE(nullptr, found_method->request());
  EXPECT_EQ(
      "struct test.fidlcodec.examples/FidlCodecTestInterfaceTableRequest {\n"
      "  table test.fidlcodec.examples/ValueTable {\n"
      "    1: int16 first_int16;\n"
      "    2: struct test.fidlcodec.examples/TwoStringStruct {\n"
      "      string value1;\n"
      "      string value2;\n"
      "    } second_struct;\n"
      "    3: reserved;\n"
      "    4: union test.fidlcodec.examples/IntStructUnion {\n"
      "      1: int32 variant_i;\n"
      "      2: struct test.fidlcodec.examples/TwoStringStruct {\n"
      "        string value1;\n"
      "        string value2;\n"
      "      } variant_tss;\n"
      "    } third_union;\n"
      "  } table;\n"
      "  int32 i;\n"
      "}",
      found_method->request()->ToString(true));

  found_method = nullptr;
  found_interface->GetMethodByFullName(
      "test.fidlcodec.examples/FidlCodecTestInterface.DefaultEnumMessage", &found_method);

  ASSERT_NE(nullptr, found_method);
  ASSERT_NE(nullptr, found_method->request());
  EXPECT_EQ(
      "struct test.fidlcodec.examples/FidlCodecTestInterfaceDefaultEnumMessageRequest {\n"
      "  enum test.fidlcodec.examples/DefaultEnum {\n"
      "    X = 23;\n"
      "  } ev;\n"
      "}",
      found_method->request()->ToString(true));

  found_method = nullptr;
  found_interface->GetMethodByFullName(
      "test.fidlcodec.examples/FidlCodecTestInterface.ShortUnionReserved", &found_method);

  ASSERT_NE(nullptr, found_method);
  ASSERT_NE(nullptr, found_method->request());
  EXPECT_EQ(
      "struct test.fidlcodec.examples/FidlCodecTestInterfaceShortUnionReservedRequest {\n"
      "  union test.fidlcodec.examples/U8U16UnionReserved {\n"
      "    1: uint8 variant_u8;\n"
      "    2: reserved;\n"
      "    3: uint16 variant_u16;\n"
      "  } u;\n"
      "  int32 i;\n"
      "}",
      found_method->request()->ToString(true));

  found_method = nullptr;
  found_interface->GetMethodByFullName("test.fidlcodec.examples/FidlCodecTestInterface.Array1",
                                       &found_method);

  ASSERT_NE(nullptr, found_method);
  ASSERT_NE(nullptr, found_method->request());
  EXPECT_EQ(
      "struct test.fidlcodec.examples/FidlCodecTestInterfaceArray1Request {\n  array<int32> "
      "b_1;\n}",
      found_method->request()->ToString(true));

  found_method = nullptr;
  found_interface->GetMethodByFullName("test.fidlcodec.examples/FidlCodecTestInterface.Vector",
                                       &found_method);

  ASSERT_NE(nullptr, found_method);
  ASSERT_NE(nullptr, found_method->request());
  EXPECT_EQ(
      "struct test.fidlcodec.examples/FidlCodecTestInterfaceVectorRequest {\n  vector<int32> "
      "v_1;\n}",
      found_method->request()->ToString(true));
}

TEST(LibraryLoader, LoadFromOrdinal) {
  LibraryReadError err;
  LibraryLoader loader;
  fidl_codec_test::FidlcodecExamples examples;
  for (const auto& element : examples.map()) {
    loader.AddContent(element.second, &err);
    ASSERT_EQ(LibraryReadError::kOk, err.value);
  }

  Library* library_ptr = loader.GetLibraryFromName("test.fidlcodec.sys");
  ASSERT_NE(library_ptr, nullptr);

  std::string kDesiredInterfaceName = "test.fidlcodec.sys/ComponentController";
  Interface* found_interface = nullptr;
  ASSERT_TRUE(library_ptr->GetInterfaceByName(kDesiredInterfaceName, &found_interface));

  const InterfaceMethod* found_method = nullptr;
  found_interface->GetMethodByFullName("test.fidlcodec.sys/ComponentController.OnDirectoryReady",
                                       &found_method);

  Ordinal64 correct_ordinal = found_method->ordinal();
  const std::vector<const InterfaceMethod*>* ordinal_methods = loader.GetByOrdinal(correct_ordinal);
  const InterfaceMethod* ordinal_method = (*ordinal_methods)[0];
  ASSERT_NE(ordinal_method, nullptr);
  ASSERT_EQ(kDesiredInterfaceName, ordinal_method->enclosing_interface().name());
  ASSERT_EQ("OnDirectoryReady", ordinal_method->name());
}

void OrdinalCompositionBody(LibraryLoader& loader) {
  Library* library_ptr = loader.GetLibraryFromName("test.fidlcodec.examples");
  ASSERT_NE(library_ptr, nullptr);

  std::string kDesiredInterfaceName = "test.fidlcodec.examples/ParamProtocol";
  Interface* found_interface = nullptr;
  ASSERT_TRUE(library_ptr->GetInterfaceByName(kDesiredInterfaceName, &found_interface));

  const InterfaceMethod* found_method = nullptr;
  found_interface->GetMethodByFullName("test.fidlcodec.examples/ParamProtocol.Method",
                                       &found_method);

  Ordinal64 correct_ordinal = found_method->ordinal();
  const std::vector<const InterfaceMethod*>* ordinal_methods = loader.GetByOrdinal(correct_ordinal);
  ASSERT_EQ(2UL, ordinal_methods->size());

  const InterfaceMethod* ordinal_method_base = (*ordinal_methods)[0];
  ASSERT_NE(ordinal_method_base, nullptr);
  ASSERT_EQ(kDesiredInterfaceName, ordinal_method_base->enclosing_interface().name());
  ASSERT_EQ("Method", ordinal_method_base->name());

  const InterfaceMethod* ordinal_method_composed = (*ordinal_methods)[1];
  ASSERT_NE(ordinal_method_composed, nullptr);
  ASSERT_EQ("test.fidlcodec.composedinto/ComposedParamProtocol",
            ordinal_method_composed->enclosing_interface().name());
  ASSERT_EQ("Method", ordinal_method_composed->name());
}

// Tests that we get the method composed into a protocol when we request a
// particular method.  The base protocol is ParamProtocol, the protocol that
// composes ParamProtocol is ComposedParamProtocol, and the method name is
// Method().  We test that we get the base protocol first in the vector
// regardless of the order that the libraries were loaded.
TEST(LibraryLoader, OrdinalComposition) {
  {
    LibraryReadError err;
    LibraryLoader loader;

    // Load the libraries in the order in examples.map().
    fidl_codec_test::FidlcodecExamples examples;
    std::vector<std::unique_ptr<std::istream>> library_files;
    for (auto element = examples.map().begin(); element != examples.map().end(); ++element) {
      loader.AddContent(element->second, &err);
      ASSERT_EQ(LibraryReadError::kOk, err.value);
    }

    OrdinalCompositionBody(loader);
  }
  {
    LibraryReadError err;
    LibraryLoader loader;

    // Load the libraries in the reverse of the order in examples.map().
    fidl_codec_test::FidlcodecExamples examples;
    std::vector<std::unique_ptr<std::istream>> library_files;
    for (auto element = examples.map().rbegin(); element != examples.map().rend(); ++element) {
      loader.AddContent(element->second, &err);
      ASSERT_EQ(LibraryReadError::kOk, err.value);
    }

    OrdinalCompositionBody(loader);
  }
}

}  // namespace fidl_codec
