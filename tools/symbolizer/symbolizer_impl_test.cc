// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/symbolizer/symbolizer_impl.h"

#include <fstream>
#include <sstream>

#include <gtest/gtest.h>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#include "src/lib/files/scoped_temp_dir.h"

namespace symbolizer {

namespace {

class SymbolizerImplTest : public ::testing::Test {
 public:
  SymbolizerImplTest() : printer_(ss_), symbolizer_(&printer_, options_) {}

 protected:
  std::stringstream ss_;
  Printer printer_;
  CommandLineOptions options_;
  SymbolizerImpl symbolizer_;
};

TEST_F(SymbolizerImplTest, Reset) {
  symbolizer_.Reset();
  ASSERT_TRUE(ss_.str().empty());

  symbolizer_.Reset();
  ASSERT_TRUE(ss_.str().empty());
}

TEST_F(SymbolizerImplTest, MMap) {
  symbolizer_.Module(0, "some_module", "deadbeef");
  symbolizer_.MMap(0x1000, 0x2000, 0, "r", 0x0);
  ASSERT_EQ(ss_.str(), "[[[ELF module #0x0 \"some_module\" BuildID=deadbeef 0x1000]]]\n");

  ss_.str("");
  symbolizer_.MMap(0x3000, 0x1000, 0, "r", 0x2000);
  ASSERT_TRUE(ss_.str().empty()) << ss_.str();

  symbolizer_.MMap(0x3000, 0x1000, 0, "r", 0x1000);
  ASSERT_EQ(ss_.str(), "symbolizer: Inconsistent base address.\n");

  ss_.str("");
  symbolizer_.MMap(0x5000, 0x1000, 1, "r", 0x0);
  ASSERT_EQ(ss_.str(), "symbolizer: Invalid module id.\n");
}

TEST_F(SymbolizerImplTest, Backtrace) {
  symbolizer_.Module(0, "some_module", "deadbeef");
  symbolizer_.MMap(0x1000, 0x2000, 0, "r", 0x0);

  ss_.str("");
  symbolizer_.Backtrace(0, 0x1004, Symbolizer::AddressType::kProgramCounter, "");
  ASSERT_EQ(ss_.str(), "   #0    0x0000000000001004 in <some_module>+0x4\n");

  ss_.str("");
  symbolizer_.Backtrace(1, 0x5000, Symbolizer::AddressType::kUnknown, "");
  ASSERT_EQ(ss_.str(), "   #1    0x0000000000005000 is not covered by any module\n");
}

TEST(SymbolizerImplTestNoFixture, OmitModuleLines) {
  std::stringstream ss;
  Printer printer(ss);
  CommandLineOptions options;
  options.omit_module_lines = true;
  SymbolizerImpl symbolizer(&printer, options);

  symbolizer.Module(0, "some_module", "deadbeef");
  symbolizer.MMap(0x1000, 0x2000, 0, "r", 0x0);
  ASSERT_EQ(ss.str(), "");
}

TEST(SymbolizerImplTestNoFixture, DumpFile) {
  // Creates a temp file first.
  files::ScopedTempDir temp_dir;
  std::string temp_file;
  ASSERT_TRUE(temp_dir.NewTempFile(&temp_file));

  {
    std::stringstream ss;
    Printer printer(ss);
    CommandLineOptions options;
    options.dumpfile_output = temp_file;
    SymbolizerImpl symbolizer(&printer, options);

    symbolizer.Module(0, "some_module", "deadbeef");
    symbolizer.MMap(0x1000, 0x2000, 0, "r", 0x0);
    symbolizer.DumpFile("type", "name");

    // Triggers the destructor of symbolizer.
  }

  std::ifstream ifs(temp_file);
  rapidjson::IStreamWrapper isw(ifs);
  rapidjson::Document d;
  d.ParseStream(isw);

  ASSERT_TRUE(d.IsArray());
  ASSERT_EQ(d.Size(), 1U);

  ASSERT_TRUE(d[0].HasMember("modules"));
  ASSERT_TRUE(d[0]["modules"].IsArray());
  ASSERT_EQ(d[0]["modules"].Size(), 1U);
  ASSERT_TRUE(d[0]["modules"][0].IsObject());

  ASSERT_TRUE(d[0].HasMember("segments"));
  ASSERT_TRUE(d[0]["segments"].IsArray());
  ASSERT_EQ(d[0]["segments"].Size(), 1U);
  ASSERT_TRUE(d[0]["segments"][0].IsObject());

  ASSERT_TRUE(d[0].HasMember("type"));
  ASSERT_TRUE(d[0]["type"].IsString());
  ASSERT_EQ(std::string(d[0]["type"].GetString()), "type");

  ASSERT_TRUE(d[0].HasMember("name"));
  ASSERT_TRUE(d[0]["name"].IsString());
  ASSERT_EQ(std::string(d[0]["name"].GetString()), "name");
}

}  // namespace

}  // namespace symbolizer
