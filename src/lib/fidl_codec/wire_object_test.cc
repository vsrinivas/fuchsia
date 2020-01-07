// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/wire_object.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "src/lib/fidl_codec/fidl_codec_test.h"
#include "src/lib/fidl_codec/json_visitor.h"
#include "src/lib/fidl_codec/wire_parser.h"
#include "test/fidlcodec/examples/cpp/fidl.h"

namespace fidl_codec {

const Colors FakeColors(/*new_reset=*/"#rst#", /*new_red=*/"#red#", /*new_green=*/"#gre#",
                        /*new_blue=*/"#blu#", /*new_white_on_magenta=*/"#wom#",
                        /*new_yellow_background=*/"#yeb#");

class WireObjectTest : public ::testing::Test {
 public:
  void TestPrintObject(const Value& value, const char* pretty_print, const char* json) {
    // Checks that we can pretty print an object (or a value).
    std::stringstream result;
    value.PrettyPrint(result, FakeColors, nullptr, "", 0, 100, 100);
    ASSERT_EQ(result.str(), pretty_print)
        << "expected = " << pretty_print << " actual = " << result.str();

    // Checks that we can use Display size.
    value.DisplaySize(1);
    value.DisplaySize(100);
    value.DisplaySize(1000);

    // Checks that we can use the JSON visitor.
    rapidjson::Document actual;
    JsonVisitor visitor(&actual, &actual.GetAllocator());
    value.Visit(&visitor);
    rapidjson::StringBuffer actual_string;
    rapidjson::Writer<rapidjson::StringBuffer> actual_w(actual_string);
    actual.Accept(actual_w);
    std::string actual_json = actual_string.GetString();
    ASSERT_EQ(json, actual_json) << "expected = " << json << " and actual = " << actual_json;
  }

 protected:
  void SetUp() override {
    loader_ = GetLoader();
    ASSERT_NE(loader_, nullptr);
  }

  LibraryLoader* loader() const { return loader_; }

 private:
  LibraryLoader* loader_;
};

#define TEST_PRINT_OBJECT(_testname, field, pretty_print, json) \
  TEST_F(WireObjectTest, Parse##_testname) { TestPrintObject(field, pretty_print, json); }

TEST_PRINT_OBJECT(EnvelopeValue, EnvelopeValue(nullptr), "#red#null#rst#", "null");

class TableValueWithNullFields : public TableValue {
 public:
  TableValueWithNullFields(LibraryLoader* loader)
      : TableValue(nullptr, GetTableDefinition(loader), 2) {
    AddMember("first_int16", nullptr);
    AddMember("third_union", nullptr);
  }

 private:
  const Table& GetTableDefinition(LibraryLoader* loader) {
    Library* library = loader->GetLibraryFromName("test.fidlcodec.examples");
    FXL_DCHECK(library != nullptr);
    const Table* table = library->GetTable("test.fidlcodec.examples/ValueTable");
    FXL_DCHECK(table != nullptr);
    return *table;
  }
};

TEST_PRINT_OBJECT(TableValue, TableValueWithNullFields(loader()), "{}", "{}");

TEST_PRINT_OBJECT(RawValue, RawValue(nullptr, std::nullopt), "", "\"\"");

}  // namespace fidl_codec
