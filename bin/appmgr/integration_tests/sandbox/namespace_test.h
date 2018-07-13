// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_INTEGRATION_TESTS_SANDBOX_NAMESPACE_TEST_H
#define GARNET_BIN_APPMGR_INTEGRATION_TESTS_SANDBOX_NAMESPACE_TEST_H

#include "gtest/gtest.h"

class NamespaceTest : public ::testing::Test {
 protected:
  // Returns whether path exists.
  bool Exists(const char* path);

  // Expect that a path exists, and fail with a descriptive message
  void ExpectExists(const char* path);

  // Expect that a path does not exist, and fail with a descriptive message
  void ExpectDoesNotExist(const char* path);
};

#endif  // GARNET_BIN_APPMGR_INTEGRATION_TESTS_SANDBOX_NAMESPACE_TEST_H
