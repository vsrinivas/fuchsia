// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>

#include <filesystem>
#include <iostream>
#include <string>

#include "src/sys/appmgr/integration_tests/sandbox/namespace_test.h"

namespace fio = fuchsia_io;

TEST_F(NamespaceTest, HasHub) {
  ExpectExists("/hub/");
  ExpectPathSupportsStrictRights("/hub",
                                 fio::wire::kOpenRightReadable | fio::wire::kOpenRightWritable);
}

TEST_F(NamespaceTest, HubInDirHasExpectedContents) {
  std::string path = "/hub/c/has_hub.cmx";
  bool there_was_at_least_one_item = false;
  for (const auto& entry : std::filesystem::directory_iterator(path)) {
    there_was_at_least_one_item = true;

    std::string svc_path = entry.path();
    svc_path.append("/in/svc");
    ExpectExists(svc_path.c_str());

    std::string pkg_path = entry.path();
    pkg_path.append("/in/pkg");
    ExpectExists(pkg_path.c_str());
  }
  ASSERT_TRUE(there_was_at_least_one_item);
}
