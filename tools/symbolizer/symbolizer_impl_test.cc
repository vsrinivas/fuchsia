// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/symbolizer/symbolizer_impl.h"

#include <sstream>

#include <gtest/gtest.h>

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
  symbolizer_.MMap(0x1000, 0x2000, 0, 0x0);
  ASSERT_EQ(ss_.str(), "[[[ELF module #0x0 \"some_module\" BuildID=deadbeef 0x1000]]]\n");

  ss_.str("");
  symbolizer_.MMap(0x3000, 0x1000, 0, 0x2000);
  ASSERT_TRUE(ss_.str().empty()) << ss_.str();

  symbolizer_.MMap(0x3000, 0x1000, 0, 0x1000);
  ASSERT_EQ(ss_.str(), "symbolizer: Inconsistent base address.\n");

  ss_.str("");
  symbolizer_.MMap(0x5000, 0x1000, 1, 0x0);
  ASSERT_EQ(ss_.str(), "symbolizer: Invalid module id.\n");
}

TEST_F(SymbolizerImplTest, Backtrace) {
  symbolizer_.Module(0, "some_module", "deadbeef");
  symbolizer_.MMap(0x1000, 0x2000, 0, 0x0);

  ss_.str("");
  symbolizer_.Backtrace(0, 0x1004, Symbolizer::AddressType::kProgramCounter, "");
  ASSERT_EQ(ss_.str(), "   #0    0x0000000000001004 in <some_module>+0x4\n");

  ss_.str("");
  symbolizer_.Backtrace(1, 0x5000, Symbolizer::AddressType::kUnknown, "");
  ASSERT_EQ(ss_.str(), "   #1    0x0000000000005000 in <\?\?\?>\n");
}

}  // namespace

}  // namespace symbolizer
