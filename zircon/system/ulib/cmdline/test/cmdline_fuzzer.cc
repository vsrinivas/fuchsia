// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cmdline/args_parser.h>

#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 2) {
    return 0;
  }

  std::vector<std::string> params;
  std::string arg0(data, data + size / 2);
  std::string arg1(data + size / 2, data + size);

  const char* argv[2] = {arg0.c_str(), arg1.c_str()};
  cmdline::GeneralArgsParser parser;
  parser.ParseGeneral(2, argv, &params);

  return 0;
}
