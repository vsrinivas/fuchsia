// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/storage/constants_and_utils.h"

#include <string>
#include <vector>

#include <lib/fxl/strings/string_view.h>

#include "gtest/gtest.h"

namespace modular {
namespace {

TEST(Storage, EncodeModulePath) {
  fidl::VectorPtr<fidl::StringPtr> fidl_array;
  for (auto s : {"foo", ":bar", "/baz"}) {
    fidl_array.push_back(s);
  }
  EXPECT_EQ("foo:\\:bar:\\/baz", EncodeModulePath(fidl_array));
}

TEST(Storage, EncodeLinkPath) {
  fidl::VectorPtr<fidl::StringPtr> fidl_array;
  for (auto s : {"foo", ":bar"}) {
    fidl_array.push_back(s);
  }

  fuchsia::modular::LinkPath link_path;
  link_path.link_name = "Fred";
  link_path.module_path = std::move(fidl_array);
  EXPECT_EQ("foo:\\:bar/Fred", EncodeLinkPath(link_path));
}

}  // namespace
}  // namespace modular
