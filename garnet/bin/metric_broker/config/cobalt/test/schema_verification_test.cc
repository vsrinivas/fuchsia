// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <string.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/schema.h"

namespace broker_service::cobalt {
namespace {

constexpr std::string_view kSchemaPath = "pkg/data/testdata/cobalt/config.schema.json";

TEST(CobaltProjectSchemaTest, JsonVerification) {
  std::ifstream config_stream(kSchemaPath.data());
  ASSERT_TRUE(config_stream.good()) << strerror(errno);
  rapidjson::IStreamWrapper config_wrapper(config_stream);
  rapidjson::Document doc;
  ASSERT_FALSE(doc.ParseStream(config_wrapper).HasParseError());
}

}  // namespace
}  // namespace broker_service::cobalt
