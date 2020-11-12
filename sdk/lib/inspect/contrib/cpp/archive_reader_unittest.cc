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
    std::vector<inspect::contrib::DiagnosticsData> data;
    rapidjson::Document doc;
    doc.Parse(R"({"moniker": "root/hub/my_component.cmx"})");
    inspect::contrib::EmplaceDiagnostics(std::move(doc), &data);
    DiagnosticsData &datum = data[0];
    EXPECT_EQ("my_component.cmx", datum.component_name());
  }
  {
    std::vector<inspect::contrib::DiagnosticsData> data;
    rapidjson::Document doc;
    doc.Parse(R"({"moniker": "abcd"})");
    inspect::contrib::EmplaceDiagnostics(std::move(doc), &data);
    DiagnosticsData &datum = data[0];
    EXPECT_EQ("abcd", datum.component_name());
  }
  {
    // Can't find path, empty return.
    std::vector<inspect::contrib::DiagnosticsData> data;
    rapidjson::Document doc;
    doc.Parse(R"({"not_moniker": "abcd"})");
    inspect::contrib::EmplaceDiagnostics(std::move(doc), &data);
    DiagnosticsData &datum = data[0];
    EXPECT_EQ("", datum.component_name());
  }
}

TEST(DiagnosticsDataTest, ContentExtraction) {
  {
    std::vector<inspect::contrib::DiagnosticsData> data;
    rapidjson::Document doc;
    doc.Parse(R"({"payload": {"value": "hello", "count": 10}})");
    inspect::contrib::EmplaceDiagnostics(std::move(doc), &data);
    DiagnosticsData &datum = data[0];
    EXPECT_EQ(rapidjson::Value("hello"), datum.GetByPath({"value"}));
    EXPECT_EQ(rapidjson::Value(10), datum.GetByPath({"count"}));
    EXPECT_EQ(rapidjson::Value(), datum.GetByPath({"value", "1234"}));
  }
  {
    std::vector<inspect::contrib::DiagnosticsData> data;
    rapidjson::Document doc;
    doc.Parse(R"({"payload": {"name/with/slashes": "hello"}})");
    inspect::contrib::EmplaceDiagnostics(std::move(doc), &data);
    DiagnosticsData &datum = data[0];
    EXPECT_EQ(rapidjson::Value("hello"), datum.GetByPath({"name/with/slashes"}));
  }
  {
    // Content is missing, return nullptr.
    std::vector<inspect::contrib::DiagnosticsData> data;
    rapidjson::Document doc;
    doc.Parse(R"({"moniker": "root/hub/my_component.cmx"})");
    inspect::contrib::EmplaceDiagnostics(std::move(doc), &data);
    DiagnosticsData &datum = data[0];
    EXPECT_EQ(rapidjson::Value(), datum.GetByPath({"value"}));
  }
}

TEST(DiagnosticsDataTest, ArrayValueCtor) {
  {
    std::vector<inspect::contrib::DiagnosticsData> data;
    rapidjson::Document doc;
    doc.Parse(R"([
      {"payload": {"value": "hello", "count": 10}},
      {"payload": {"value": "world", "count": 40}}
    ])");

    inspect::contrib::EmplaceDiagnostics(std::move(doc), &data);
    DiagnosticsData &first = data[0];
    DiagnosticsData &second = data[1];

    EXPECT_EQ(rapidjson::Value("hello"), first.GetByPath({"value"}));
    EXPECT_EQ(rapidjson::Value(10), first.GetByPath({"count"}));
    EXPECT_EQ(rapidjson::Value(), first.GetByPath({"value", "1234"}));

    EXPECT_EQ(rapidjson::Value("world"), second.GetByPath({"value"}));
    EXPECT_EQ(rapidjson::Value(40), second.GetByPath({"count"}));
    EXPECT_EQ(rapidjson::Value(), second.GetByPath({"value", "1234"}));
  }
}

}  // namespace
