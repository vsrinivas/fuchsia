// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <zircon/assert.h>

#include <zxtest/zxtest.h>

namespace {

TEST(Binding, CreateStringPropertyValues) {
  zx_device_str_prop_val int_prop_val = str_prop_int_val(1);
  ASSERT_EQ(ZX_DEVICE_PROPERTY_VALUE_INT, int_prop_val.value_type);
  ASSERT_EQ(1, int_prop_val.value.int_val);

  zx_device_str_prop_val bool_prop_val = str_prop_bool_val(true);
  ASSERT_EQ(ZX_DEVICE_PROPERTY_VALUE_BOOL, bool_prop_val.value_type);
  ASSERT_TRUE(bool_prop_val.value.bool_val);

  std::string str_val = "magpie";
  zx_device_str_prop_val str_prop_val = str_prop_str_val(str_val.c_str());
  ASSERT_EQ(ZX_DEVICE_PROPERTY_VALUE_STRING, str_prop_val.value_type);
  ASSERT_STREQ(str_val, str_prop_val.value.str_val);
}

}  // namespace
