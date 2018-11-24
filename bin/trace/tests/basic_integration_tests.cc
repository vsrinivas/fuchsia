// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/tests/basic_integration_tests.h"

// Basic tests are all folded into integration_test_app.

const IntegrationTest* kIntegrationTests[] = {
  &kFillBufferIntegrationTest,
  &kSimpleIntegrationTest,
};

const IntegrationTest* LookupTest(const std::string& test_name) {
  for (auto test : kIntegrationTests) {
    if (test->name == test_name)
      return test;
  }
  return nullptr;
}
