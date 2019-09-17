// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "library_loader.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/lib/fidl_codec/library_loader_test_data.h"

namespace fidl_codec {

TEST(LibraryLoader, LoadSimple) {
  fidl_codec_test::ExampleMap examples;
  std::vector<std::unique_ptr<std::istream>> library_files;
  for (const auto& element : examples.map()) {
    std::unique_ptr<std::istream> file =
        std::make_unique<std::istringstream>(std::istringstream(element.second));

    library_files.push_back(std::move(file));
  }
  LibraryReadError err;
  LibraryLoader loader = LibraryLoader(&library_files, &err);
  ASSERT_EQ(LibraryReadError::kOk, err.value);

  Library* library_ptr = loader.GetLibraryFromName("fidl.test.frobinator");

  std::string kDesiredInterfaceName = "fidl.test.frobinator/Frobinator";
  const Interface* found_interface = nullptr;
  ASSERT_TRUE(library_ptr->GetInterfaceByName(kDesiredInterfaceName, &found_interface));

  ASSERT_NE(found_interface, nullptr) << "Could not find interface " << kDesiredInterfaceName;

  std::string kDesiredFullMethodName = "fidl.test.frobinator/Frobinator.Frob";
  const InterfaceMethod* found_method = nullptr;
  found_interface->GetMethodByFullName(kDesiredFullMethodName, &found_method);

  ASSERT_NE(found_method, nullptr) << "Could not find method " << kDesiredFullMethodName;
}

// Makes sure that loading works when you load one IR at a time, instead of in a bunch.
TEST(LibraryLoader, LoadSimpleOneAtATime) {
  fidl_codec_test::ExampleMap examples;
  LibraryLoader loader;
  LibraryReadError err;
  for (const auto& element : examples.map()) {
    std::unique_ptr<std::istream> file =
        std::make_unique<std::istringstream>(std::istringstream(element.second));
    loader.Add(&file, &err);
    ASSERT_EQ(LibraryReadError::kOk, err.value);
  }

  Library* library_ptr = loader.GetLibraryFromName("fidl.test.frobinator");

  std::string kDesiredInterfaceName = "fidl.test.frobinator/Frobinator";
  const Interface* found_interface = nullptr;
  ASSERT_TRUE(library_ptr->GetInterfaceByName(kDesiredInterfaceName, &found_interface));

  ASSERT_NE(found_interface, nullptr) << "Could not find interface " << kDesiredInterfaceName;

  std::string kDesiredFullMethodName = "fidl.test.frobinator/Frobinator.Frob";
  const InterfaceMethod* found_method = nullptr;
  found_interface->GetMethodByFullName(kDesiredFullMethodName, &found_method);

  ASSERT_NE(found_method, nullptr) << "Could not find method " << kDesiredFullMethodName;
}

// Ensure that, if you load two libraries with the same name, the last one in the list is the one
// that sticks.
TEST(LibraryLoader, LoadSecondWins) {
  fidl_codec_test::ExampleMap examples;
  std::vector<std::unique_ptr<std::istream>> library_files;
  std::string frobinator_value;
  const std::string file_to_replace = "frobinator.fidl.json";
  for (const auto& element : examples.map()) {
    std::unique_ptr<std::istream> file =
        std::make_unique<std::istringstream>(std::istringstream(element.second));

    library_files.push_back(std::move(file));
    if (0 == element.first.compare(element.first.length() - file_to_replace.length(),
                                   file_to_replace.length(), file_to_replace)) {
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
  std::unique_ptr<std::istream> file =
      std::make_unique<std::istringstream>(std::istringstream(frobinator_value));
  library_files.push_back(std::move(file));

  LibraryReadError err;
  LibraryLoader loader = LibraryLoader(&library_files, &err);
  ASSERT_EQ(LibraryReadError::kOk, err.value);

  Library* library_ptr = loader.GetLibraryFromName("fidl.test.frobinator");

  std::string kDesiredInterfaceName = "fidl.test.frobinator/Frobinator";
  const Interface* found_interface = nullptr;
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
}

TEST(LibraryLoader, LoadFromOrdinal) {
  fidl_codec_test::ExampleMap examples;
  std::vector<std::unique_ptr<std::istream>> library_files;
  for (const auto& element : examples.map()) {
    std::unique_ptr<std::istream> file =
        std::make_unique<std::istringstream>(std::istringstream(element.second));

    library_files.push_back(std::move(file));
  }
  LibraryReadError err;
  LibraryLoader loader = LibraryLoader(&library_files, &err);
  ASSERT_EQ(LibraryReadError::kOk, err.value);

  Library* library_ptr = loader.GetLibraryFromName("test.fidlcodec.sys");
  ASSERT_NE(library_ptr, nullptr);

  std::string kDesiredInterfaceName = "test.fidlcodec.sys/ComponentController";
  const Interface* found_interface = nullptr;
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

  Ordinal64 correct_old_ordinal = found_method->old_ordinal();
  const std::vector<const InterfaceMethod*>* old_ordinal_methods =
      loader.GetByOrdinal(correct_old_ordinal);
  const InterfaceMethod* old_ordinal_method = (*old_ordinal_methods)[0];
  ASSERT_NE(old_ordinal_method, nullptr);
  ASSERT_EQ(kDesiredInterfaceName, old_ordinal_method->enclosing_interface().name());
  ASSERT_EQ("OnDirectoryReady", old_ordinal_method->name());
}

void OrdinalCompositionBody(std::vector<std::unique_ptr<std::istream>>* library_files) {
  LibraryReadError err;
  LibraryLoader loader = LibraryLoader(library_files, &err);
  ASSERT_EQ(LibraryReadError::kOk, err.value);

  Library* library_ptr = loader.GetLibraryFromName("test.fidlcodec.examples");
  ASSERT_NE(library_ptr, nullptr);

  std::string kDesiredInterfaceName = "test.fidlcodec.examples/ParamProtocol";
  const Interface* found_interface = nullptr;
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

  Ordinal64 correct_old_ordinal = found_method->old_ordinal();
  const std::vector<const InterfaceMethod*>* old_ordinal_methods =
      loader.GetByOrdinal(correct_old_ordinal);
  const InterfaceMethod* old_ordinal_method = (*old_ordinal_methods)[0];
  ASSERT_NE(old_ordinal_method, nullptr);
  ASSERT_EQ(kDesiredInterfaceName, old_ordinal_method->enclosing_interface().name());
  ASSERT_EQ("Method", old_ordinal_method->name());
}

// Tests that we get the method composed into a protocol when we request a
// particular method.  The base protocol is ParamProtocol, the protocol that
// composes ParamProtocol is ComposedParamProtocol, and the method name is
// Method().  We test that we get the base protocol first in the vector
// regardless of the order that the libraries were loaded.
TEST(LibraryLoader, OrdinalComposition) {
  {
    // Load the libraries in the order in examples.map().
    fidl_codec_test::ExampleMap examples;
    std::vector<std::unique_ptr<std::istream>> library_files;
    for (const auto& element : examples.map()) {
      std::unique_ptr<std::istream> file =
          std::make_unique<std::istringstream>(std::istringstream(element.second));

      library_files.push_back(std::move(file));
    }

    OrdinalCompositionBody(&library_files);
  }
  {
    // Load the libraries in the reverse of the order in examples.map().
    fidl_codec_test::ExampleMap examples;
    std::vector<std::unique_ptr<std::istream>> library_files;
    for (const auto& element : examples.map()) {
      std::unique_ptr<std::istream> file =
          std::make_unique<std::istringstream>(std::istringstream(element.second));

      library_files.insert(library_files.begin(), std::move(file));
    }
  }
}

}  // namespace fidl_codec
