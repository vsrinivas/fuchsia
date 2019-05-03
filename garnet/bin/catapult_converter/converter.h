// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CATAPULT_CONVERTER_CONVERTER_H_
#define GARNET_BIN_CATAPULT_CONVERTER_CONVERTER_H_

#include "rapidjson/document.h"

// Generate a 128-bit (pseudo) random UUID in the form of version 4 as described
// in RFC 4122, section 4.4.
// The format of UUID version 4 must be xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx,
// where y is one of [8, 9, A, B].
// The hexadecimal values "a" through "f" are output as lower case characters.
// If UUID generation fails an empty string is returned.
std::string GenerateUuid();

struct ConverterArgs {
  // These parameters are copied into the Catapult histogram file.  See the
  // README.md file for the meanings of these parameters.
  int64_t timestamp = 0;
  const char* masters = nullptr;
  const char* bots = nullptr;
  const char* log_url = nullptr;

  // Generate deterministic GUIDs instead of random GUIDs.  This is used
  // only for testing.
  bool use_test_guids = false;
};

void Convert(rapidjson::Document* input, rapidjson::Document* output,
             const ConverterArgs* args);
int ConverterMain(int argc, char** argv);

#endif  // GARNET_BIN_CATAPULT_CONVERTER_CONVERTER_H_
