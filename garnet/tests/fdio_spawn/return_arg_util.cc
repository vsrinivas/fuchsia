// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fxl/strings/string_number_conversions.h"

int main(int argc, const char* argv[]) {
  if (argc < 2) {
    return -1;
  }

  const char* arg = argv[1];
  int number;
  if (!fxl::StringToNumberWithError<int>(arg, &number)) {
    return -1;
  }
  return number;
}
