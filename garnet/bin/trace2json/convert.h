// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE2JSON_CONVERT_H_
#define GARNET_BIN_TRACE2JSON_CONVERT_H_

#include <string>

struct ConvertSettings {
  std::string input_file_name;
  std::string output_file_name;
  bool compressed_input = false;
  bool compressed_output = false;
};

bool ConvertTrace(ConvertSettings);

#endif  // GARNET_BIN_TRACE2JSON_CONVERT_H_
