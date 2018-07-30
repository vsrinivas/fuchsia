// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <fuchsia/sys/cpp/fidl.h>
#include "lib/component/cpp/environment_services.h"

#include "garnet/bin/appmgr/integration_tests/sandbox/namespace_test.h"

NamespaceTest::NamespaceTest() {
  component::ConnectToEnvironmentService(env_.NewRequest());
  env_->GetServices(service_provider_.NewRequest());
}

bool NamespaceTest::Exists(const char* path) {
  struct stat stat_;
  return stat(path, &stat_) == 0;
}

void NamespaceTest::ExpectExists(const char* path) {
  EXPECT_TRUE(Exists(path)) << "Can't find " << path << ": " << strerror(errno);
}

void NamespaceTest::ExpectDoesNotExist(const char* path) {
  EXPECT_FALSE(Exists(path)) << "Unexpectedly found " << path;
}

TEST_F(NamespaceTest, SanityCheck) {
  ExpectExists("/svc/");
  ExpectDoesNotExist("/this_should_not_exist");
}
