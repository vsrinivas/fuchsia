// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "third_party/rapidjson/rapidjson/document.h"

struct ConverterArgs {
  // These parameters are copied into the Catapult histogram file.  See the
  // README.md file for the meanings of these parameters.
  long timestamp = 0;
  const char* masters = nullptr;
  const char* test_suite = nullptr;
  const char* bots = nullptr;

  // Generate deterministic GUIDs instead of random GUIDs.  This is used
  // only for testing.
  bool use_test_guids = false;
};

void Convert(rapidjson::Document* input, rapidjson::Document* output,
             const ConverterArgs* args);
int ConverterMain(int argc, char** argv);
