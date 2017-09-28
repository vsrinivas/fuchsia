// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/ledger_client/storage.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "lib/fxl/strings/string_view.h"

namespace modular {
namespace {

TEST(Storage, EncodeModulePath) {
  fidl::Array<fidl::String> fidl_array;
  for (auto s : {"foo", ":bar", "/baz"}) {
    fidl_array.push_back(s);
  }
  EXPECT_EQ("foo:\\:bar:\\/baz", EncodeModulePath(fidl_array));
}

TEST(Storage, EncodeLinkPath) {
  fidl::Array<fidl::String> fidl_array;
  for (auto s : {"foo", ":bar"}) {
    fidl_array.push_back(s);
  }

  auto link_path = LinkPath::New();
  link_path->link_name = "Fred";
  link_path->module_path = std::move(fidl_array);
  EXPECT_EQ("foo:\\:bar/Fred", EncodeLinkPath(link_path));
}

}  // namespace
}  // namespace modular
