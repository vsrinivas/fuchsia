// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "garnet/bin/fidlcat/lib/library_loader_test_data.h"
#include "gtest/gtest.h"
#include "library_loader.h"

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
}

}  // namespace fidlcat
