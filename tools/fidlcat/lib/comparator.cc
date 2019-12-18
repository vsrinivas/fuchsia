// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/comparator.h"

#include <fstream>
#include <iostream>

#include "src/lib/fxl/logging.h"

namespace fidlcat {

void Comparator::Compare(std::ostream& os) {
  std::fstream compare_file;
  compare_file.open(compare_file_name_);
  std::istringstream actual_output(output_stream_.str());
  std::string actual, expected;

  while (std::getline(actual_output, actual)) {
    if (!std::getline(compare_file, expected)) {
      os << "Actual output was longer\n";
      compare_file.close();
      return;
    }
    if (actual.compare(expected) != 0) {
      os << "Expected: \"" << expected << "\"\nActual:   \"" << actual << "\"\n";
      compare_file.close();
      return;
    }
  }
  if (std::getline(compare_file, expected)) {
    os << "Expected output was longer\n";
    compare_file.close();
    return;
  }
  os << "Identical output and expected\n";
  compare_file.close();
}

}  // namespace fidlcat
