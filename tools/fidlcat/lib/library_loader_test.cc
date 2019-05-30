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
    std::unique_ptr<std::istream> file = std::make_unique<std::istringstream>(
        std::istringstream(element.second));

    library_files.push_back(std::move(file));
  }
  LibraryReadError err;
  LibraryLoader loader = LibraryLoader(library_files, &err);
  ASSERT_EQ(LibraryReadError::kOk, err.value);

  Library* library_ptr = loader.GetLibraryFromName("fidl.test.frobinator");

  std::string kDesiredInterfaceName = "fidl.test.frobinator/Frobinator";
  const Interface* found_interface = nullptr;
  ASSERT_TRUE(
      library_ptr->GetInterfaceByName(kDesiredInterfaceName, &found_interface));

  ASSERT_NE(found_interface, nullptr)
      << "Could not find interface " << kDesiredInterfaceName;

  std::string kDesiredFullMethodName = "fidl.test.frobinator/Frobinator.Frob";
  const InterfaceMethod* found_method = nullptr;
  found_interface->GetMethodByFullName(kDesiredFullMethodName, &found_method);

  ASSERT_NE(found_method, nullptr)
      << "Could not find method " << kDesiredFullMethodName;
}

TEST(LibraryLoader, LoadFromOrdinal) {
  fidlcat_test::ExampleMap examples;
  std::vector<std::unique_ptr<std::istream>> library_files;
  for (auto element : examples.map()) {
    std::unique_ptr<std::istream> file = std::make_unique<std::istringstream>(
        std::istringstream(element.second));

    library_files.push_back(std::move(file));
  }
  LibraryReadError err;
  LibraryLoader loader = LibraryLoader(library_files, &err);
  ASSERT_EQ(LibraryReadError::kOk, err.value);

  Library* library_ptr = loader.GetLibraryFromName("test.fidlcat.sys");
  ASSERT_NE(library_ptr, nullptr);

  std::string kDesiredInterfaceName = "test.fidlcat.sys/ComponentController";
  const Interface* found_interface = nullptr;
  ASSERT_TRUE(
      library_ptr->GetInterfaceByName(kDesiredInterfaceName, &found_interface));

  const InterfaceMethod* found_method = nullptr;
  found_interface->GetMethodByFullName(
      "test.fidlcat.sys/ComponentController.OnDirectoryReady", &found_method);

  Ordinal correct_ordinal = found_method->ordinal();
  const InterfaceMethod* ordinal_method = loader.GetByOrdinal(correct_ordinal);
  ASSERT_NE(ordinal_method, nullptr);
  ASSERT_EQ(kDesiredInterfaceName,
            ordinal_method->enclosing_interface().name());
  ASSERT_EQ("OnDirectoryReady", ordinal_method->name());
}

}  // namespace fidlcat
