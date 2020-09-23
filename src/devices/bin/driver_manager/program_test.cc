// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/program.h"

#include <gtest/gtest.h>

namespace fdata = llcpp::fuchsia::data;

TEST(ProgramTest, ProgramValue) {
  fidl::StringView str = "value-for-str";
  fidl::VectorView<fidl::StringView> strvec;
  fdata::DictionaryEntry program_entries[] = {
      {
          .key = "key-for-str",
          .value = fdata::DictionaryValue::WithStr(fidl::unowned_ptr(&str)),
      },
      {
          .key = "key-for-strvec",
          .value = fdata::DictionaryValue::WithStrVec(fidl::unowned_ptr(&strvec)),
      },
  };
  auto entries = fidl::unowned_vec(program_entries);
  auto program = fdata::Dictionary::Builder(std::make_unique<fdata::Dictionary::Frame>())
                     .set_entries(fidl::unowned_ptr(&entries))
                     .build();

  EXPECT_EQ("value-for-str", program_value(program, "key-for-str").value());
  EXPECT_EQ(ZX_ERR_WRONG_TYPE, program_value(program, "key-for-strvec").error_value());
  EXPECT_EQ(ZX_ERR_NOT_FOUND, program_value(program, "key-unkown").error_value());
}
