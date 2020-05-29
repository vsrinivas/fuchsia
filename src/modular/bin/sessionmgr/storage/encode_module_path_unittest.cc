// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/encode_module_path.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace modular {
namespace {

TEST(Storage, EncodeModulePath) {
  std::vector<std::string> fidl_array = {"foo", ":bar", "/baz"};
  EXPECT_EQ("foo:\\:bar:\\/baz", EncodeModulePath(fidl_array));
}

}  // namespace
}  // namespace modular
