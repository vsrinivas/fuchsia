// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "library_loader.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "tools/fidlcat/lib/library_loader_test_data.h"

namespace fidlcat {

TEST(LibraryLoader, LoadSimple) {
  fidlcat_test::ExampleMap examples;
  std::vector<std::unique_ptr<std::istream>> library_files;
  for (auto element : examples.map()) {
    std::unique_ptr<std::istream> file =
        std::make_unique<std::istringstream>(std::istringstream(element.second));

    library_files.push_back(std::move(file));
  }
  LibraryReadError err;
  LibraryLoader loader = LibraryLoader(library_files, &err);
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

TEST(LibraryLoader, LoadFromOrdinal) {
  fidlcat_test::ExampleMap examples;
  std::vector<std::unique_ptr<std::istream>> library_files;
  for (auto element : examples.map()) {
    std::unique_ptr<std::istream> file =
        std::make_unique<std::istringstream>(std::istringstream(element.second));

    library_files.push_back(std::move(file));
  }
  LibraryReadError err;
  LibraryLoader loader = LibraryLoader(library_files, &err);
  ASSERT_EQ(LibraryReadError::kOk, err.value);

  Library* library_ptr = loader.GetLibraryFromName("test.fidlcat.sys");
  ASSERT_NE(library_ptr, nullptr);

  std::string kDesiredInterfaceName = "test.fidlcat.sys/ComponentController";
  const Interface* found_interface = nullptr;
  ASSERT_TRUE(library_ptr->GetInterfaceByName(kDesiredInterfaceName, &found_interface));

  const InterfaceMethod* found_method = nullptr;
  found_interface->GetMethodByFullName("test.fidlcat.sys/ComponentController.OnDirectoryReady",
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

void OrdinalCompositionBody(std::vector<std::unique_ptr<std::istream>>& library_files) {
  LibraryReadError err;
  LibraryLoader loader = LibraryLoader(library_files, &err);
  ASSERT_EQ(LibraryReadError::kOk, err.value);

  Library* library_ptr = loader.GetLibraryFromName("test.fidlcat.examples");
  ASSERT_NE(library_ptr, nullptr);

  std::string kDesiredInterfaceName = "test.fidlcat.examples/ParamProtocol";
  const Interface* found_interface = nullptr;
  ASSERT_TRUE(library_ptr->GetInterfaceByName(kDesiredInterfaceName, &found_interface));

  const InterfaceMethod* found_method = nullptr;
  found_interface->GetMethodByFullName("test.fidlcat.examples/ParamProtocol.Method", &found_method);

  Ordinal64 correct_ordinal = found_method->ordinal();
  const std::vector<const InterfaceMethod*>* ordinal_methods = loader.GetByOrdinal(correct_ordinal);
  ASSERT_EQ(2UL, ordinal_methods->size());

  const InterfaceMethod* ordinal_method_base = (*ordinal_methods)[0];
  ASSERT_NE(ordinal_method_base, nullptr);
  ASSERT_EQ(kDesiredInterfaceName, ordinal_method_base->enclosing_interface().name());
  ASSERT_EQ("Method", ordinal_method_base->name());

  const InterfaceMethod* ordinal_method_composed = (*ordinal_methods)[1];
  ASSERT_NE(ordinal_method_composed, nullptr);
  ASSERT_EQ("test.fidlcat.composedinto/ComposedParamProtocol",
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
    fidlcat_test::ExampleMap examples;
    std::vector<std::unique_ptr<std::istream>> library_files;
    for (auto element : examples.map()) {
      std::unique_ptr<std::istream> file =
          std::make_unique<std::istringstream>(std::istringstream(element.second));

      library_files.push_back(std::move(file));
    }

    OrdinalCompositionBody(library_files);
  }
  {
    // Load the libraries in the reverse of the order in examples.map().
    fidlcat_test::ExampleMap examples;
    std::vector<std::unique_ptr<std::istream>> library_files;
    for (auto element : examples.map()) {
      std::unique_ptr<std::istream> file =
          std::make_unique<std::istringstream>(std::istringstream(element.second));

      library_files.insert(library_files.begin(), std::move(file));
    }
  }
}

}  // namespace fidlcat
