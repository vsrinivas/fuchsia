// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/contrib/cpp/archive_reader.h>

#include <rapidjson/document.h>
#include <zxtest/zxtest.h>

namespace {

using inspect::contrib::DiagnosticsData;

TEST(DiagnosticsDataTest, ComponentNameExtraction) {
  {
    rapidjson::Document doc;
    doc.Parse(R"({"path": "root/hub/my_component.cmx"})");
    DiagnosticsData data(std::move(doc));
    EXPECT_EQ("my_component.cmx", data.component_name());
  }
  {
    rapidjson::Document doc;
    doc.Parse(R"({"path": "abcd"})");
    DiagnosticsData data(std::move(doc));
    EXPECT_EQ("abcd", data.component_name());
  }
  {
    // Can't find path, empty return.
    rapidjson::Document doc;
    doc.Parse(R"({"not_path": "abcd"})");
    DiagnosticsData data(std::move(doc));
    EXPECT_EQ("", data.component_name());
  }
}

TEST(DiagnosticsDataTest, ContentExtraction) {
  {
    rapidjson::Document doc;
    doc.Parse(R"({"contents": {"value": "hello", "count": 10}})");
    DiagnosticsData data(std::move(doc));
    EXPECT_EQ(rapidjson::Value("hello"), data.GetByPath({"value"}));
    EXPECT_EQ(rapidjson::Value(10), data.GetByPath({"count"}));
    EXPECT_EQ(rapidjson::Value(), data.GetByPath({"value", "1234"}));
  }
  {
    rapidjson::Document doc;
    doc.Parse(R"({"contents": {"name/with/slashes": "hello"}})");
    DiagnosticsData data(std::move(doc));
    EXPECT_EQ(rapidjson::Value("hello"), data.GetByPath({"name/with/slashes"}));
  }
  {
    // Content is missing, return nullptr.
    rapidjson::Document doc;
    doc.Parse(R"({"path": "root/hub/my_component.cmx"})");
    DiagnosticsData data(std::move(doc));
    EXPECT_EQ(rapidjson::Value(), data.GetByPath({"value"}));
  }
}

}  // namespace
