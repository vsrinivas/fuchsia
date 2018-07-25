// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/program_metadata.h"

#include "gtest/gtest.h"

#include "third_party/rapidjson/rapidjson/document.h"

namespace component {
namespace {

TEST(ProgramMetadata, Parse) {
  rapidjson::Document document;
  document.Parse(R"JSON({ "binary": "bin/app" })JSON");
  ProgramMetadata program;
  EXPECT_TRUE(program.IsNull());
  EXPECT_TRUE(program.Parse(document));
  EXPECT_FALSE(program.IsNull());
  EXPECT_EQ("bin/app", program.binary());
}

}  // namespace
}  // namespace component
