// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/device.h>

#include <memory>

#include <ddktl/device.h>
#include <zxtest/zxtest.h>

namespace {

class DeviceGroupTest : public zxtest::Test {};

TEST_F(DeviceGroupTest, CreateAcceptBindRules) {
  auto int_key_bind_rule = ddk::BindRuleAcceptInt(5, 100);
  ASSERT_EQ(5, int_key_bind_rule.get().key.data.int_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, int_key_bind_rule.get().condition);
  ASSERT_EQ(1, int_key_bind_rule.get().values_count);
  ASSERT_EQ(100, int_key_bind_rule.get().values[0].data.int_value);

  auto int_val_bind_rule = ddk::BindRuleAcceptInt("int_based_val", 50);
  ASSERT_EQ("int_based_val", int_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, int_val_bind_rule.get().condition);
  ASSERT_EQ(1, int_val_bind_rule.get().values_count);
  ASSERT_EQ(50, int_val_bind_rule.get().values[0].data.int_value);

  auto str_val_bind_rule = ddk::BindRuleAcceptString("string_based_val", "thrush");
  ASSERT_EQ("string_based_val", str_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, str_val_bind_rule.get().condition);
  ASSERT_EQ(1, str_val_bind_rule.get().values_count);
  ASSERT_EQ("thrush", str_val_bind_rule.get().values[0].data.str_value);

  auto bool_val_bind_rule = ddk::BindRuleAcceptBool("bool_based_val", true);
  ASSERT_EQ("bool_based_val", bool_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, bool_val_bind_rule.get().condition);
  ASSERT_EQ(1, bool_val_bind_rule.get().values_count);
  ASSERT_TRUE(bool_val_bind_rule.get().values[0].data.bool_value);

  auto enum_val_bind_rule =
      ddk::BindRuleAcceptEnum("enum_based_val", "fuchsia.gpio.BIND_PROTOCOL.DEVICE");
  ASSERT_EQ("enum_based_val", enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(1, enum_val_bind_rule.get().values_count);
  ASSERT_EQ("fuchsia.gpio.BIND_PROTOCOL.DEVICE",
            enum_val_bind_rule.get().values[0].data.enum_value);
}

TEST_F(DeviceGroupTest, CreateRejectBindRules) {
  auto int_key_bind_rule = ddk::BindRuleRejectInt(5, 100);
  ASSERT_EQ(5, int_key_bind_rule.get().key.data.int_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, int_key_bind_rule.get().condition);
  ASSERT_EQ(1, int_key_bind_rule.get().values_count);
  ASSERT_EQ(100, int_key_bind_rule.get().values[0].data.int_value);

  auto int_val_bind_rule = ddk::BindRuleRejectInt("int_based_val", 50);
  ASSERT_EQ("int_based_val", int_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, int_val_bind_rule.get().condition);
  ASSERT_EQ(1, int_val_bind_rule.get().values_count);
  ASSERT_EQ(50, int_val_bind_rule.get().values[0].data.int_value);

  auto str_val_bind_rule = ddk::BindRuleRejectString("string_based_val", "thrush");
  ASSERT_EQ("string_based_val", str_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, str_val_bind_rule.get().condition);
  ASSERT_EQ(1, str_val_bind_rule.get().values_count);
  ASSERT_EQ("thrush", str_val_bind_rule.get().values[0].data.str_value);

  auto bool_val_bind_rule = ddk::BindRuleRejectBool("bool_based_val", true);
  ASSERT_EQ("bool_based_val", bool_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, bool_val_bind_rule.get().condition);
  ASSERT_EQ(1, bool_val_bind_rule.get().values_count);
  ASSERT_TRUE(bool_val_bind_rule.get().values[0].data.bool_value);

  auto enum_val_bind_rule =
      ddk::BindRuleRejectEnum("enum_based_val", "fuchsia.gpio.BIND_PROTOCOL.DEVICE");
  ASSERT_EQ("enum_based_val", enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(1, enum_val_bind_rule.get().values_count);
  ASSERT_EQ("fuchsia.gpio.BIND_PROTOCOL.DEVICE",
            enum_val_bind_rule.get().values[0].data.enum_value);
}

TEST_F(DeviceGroupTest, CreateAcceptBindRuleList) {
  const uint32_t int_key_bind_rule_values[] = {10, 3};
  auto int_key_bind_rule = ddk::BindRuleAcceptIntList(5, int_key_bind_rule_values);
  ASSERT_EQ(5, int_key_bind_rule.get().key.data.int_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, int_key_bind_rule.get().condition);
  ASSERT_EQ(2, int_key_bind_rule.get().values_count);
  ASSERT_EQ(10, int_key_bind_rule.get().values[0].data.int_value);
  ASSERT_EQ(3, int_key_bind_rule.get().values[1].data.int_value);

  const uint32_t int_val_bind_rule_values[] = {20, 150, 8};
  auto int_val_bind_rule = ddk::BindRuleAcceptIntList("int_based_val", int_val_bind_rule_values);
  ASSERT_EQ("int_based_val", int_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, int_val_bind_rule.get().condition);
  ASSERT_EQ(3, int_val_bind_rule.get().values_count);
  ASSERT_EQ(20, int_val_bind_rule.get().values[0].data.int_value);
  ASSERT_EQ(150, int_val_bind_rule.get().values[1].data.int_value);
  ASSERT_EQ(8, int_val_bind_rule.get().values[2].data.int_value);

  const char* str_val_bind_rule_values[] = {"thrush", "robin"};
  auto str_val_bind_rule =
      ddk::BindRuleAcceptStringList("string_based_val", str_val_bind_rule_values);
  ASSERT_EQ("string_based_val", str_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, str_val_bind_rule.get().condition);
  ASSERT_EQ(2, str_val_bind_rule.get().values_count);
  ASSERT_EQ("thrush", str_val_bind_rule.get().values[0].data.str_value);
  ASSERT_EQ("robin", str_val_bind_rule.get().values[1].data.str_value);

  const char* enum_val_bind_rule_values[] = {"fuchsia.gpio.BIND_PROTOCOL.DEVICE",
                                             "fuchsia.gpio.BIND_PROTOCOL.IMPL"};
  auto enum_val_bind_rule =
      ddk::BindRuleAcceptEnumList("enum_based_val", enum_val_bind_rule_values);
  ASSERT_EQ("enum_based_val", enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(2, enum_val_bind_rule.get().values_count);
  ASSERT_EQ("fuchsia.gpio.BIND_PROTOCOL.DEVICE",
            enum_val_bind_rule.get().values[0].data.enum_value);
  ASSERT_EQ("fuchsia.gpio.BIND_PROTOCOL.IMPL", enum_val_bind_rule.get().values[1].data.enum_value);
}

TEST_F(DeviceGroupTest, CreateRejectBindRuleList) {
  const uint32_t int_key_bind_rule_values[] = {10, 3};
  auto int_key_bind_rule = ddk::BindRuleRejectIntList(5, int_key_bind_rule_values);
  ASSERT_EQ(5, int_key_bind_rule.get().key.data.int_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, int_key_bind_rule.get().condition);
  ASSERT_EQ(2, int_key_bind_rule.get().values_count);
  ASSERT_EQ(10, int_key_bind_rule.get().values[0].data.int_value);
  ASSERT_EQ(3, int_key_bind_rule.get().values[1].data.int_value);

  const uint32_t int_val_bind_rule_values[] = {20, 150, 8};
  auto int_val_bind_rule = ddk::BindRuleRejectIntList("int_based_val", int_val_bind_rule_values);
  ASSERT_EQ("int_based_val", int_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, int_val_bind_rule.get().condition);
  ASSERT_EQ(3, int_val_bind_rule.get().values_count);
  ASSERT_EQ(20, int_val_bind_rule.get().values[0].data.int_value);
  ASSERT_EQ(150, int_val_bind_rule.get().values[1].data.int_value);
  ASSERT_EQ(8, int_val_bind_rule.get().values[2].data.int_value);

  const char* str_val_bind_rule_values[] = {"thrush", "robin"};
  auto str_val_bind_rule =
      ddk::BindRuleRejectStringList("string_based_val", str_val_bind_rule_values);
  ASSERT_EQ("string_based_val", str_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, str_val_bind_rule.get().condition);
  ASSERT_EQ(2, str_val_bind_rule.get().values_count);
  ASSERT_EQ("thrush", str_val_bind_rule.get().values[0].data.str_value);
  ASSERT_EQ("robin", str_val_bind_rule.get().values[1].data.str_value);

  const char* enum_val_bind_rule_values[] = {"fuchsia.gpio.BIND_PROTOCOL.DEVICE",
                                             "fuchsia.gpio.BIND_PROTOCOL.IMPL"};
  auto enum_val_bind_rule =
      ddk::BindRuleRejectEnumList("enum_based_val", enum_val_bind_rule_values);
  ASSERT_EQ("enum_based_val", enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(2, enum_val_bind_rule.get().values_count);
  ASSERT_EQ("fuchsia.gpio.BIND_PROTOCOL.DEVICE",
            enum_val_bind_rule.get().values[0].data.enum_value);
  ASSERT_EQ("fuchsia.gpio.BIND_PROTOCOL.IMPL", enum_val_bind_rule.get().values[1].data.enum_value);
}

TEST_F(DeviceGroupTest, CreateBindProperties) {
  auto int_key_bind_prop = ddk::BindPropertyInt(1, 100);
  ASSERT_EQ(1, int_key_bind_prop.key.data.int_key);
  ASSERT_EQ(100, int_key_bind_prop.value.data.int_value);

  auto int_val_bind_prop = ddk::BindPropertyInt("int_key", 20);
  ASSERT_EQ("int_key", int_val_bind_prop.key.data.str_key);
  ASSERT_EQ(20, int_val_bind_prop.value.data.int_value);

  auto str_val_bind_prop = ddk::BindPropertyString("str_key", "thrush");
  ASSERT_EQ("str_key", str_val_bind_prop.key.data.str_key);
  ASSERT_EQ("thrush", str_val_bind_prop.value.data.str_value);

  auto bool_val_bind_prop = ddk::BindPropertyBool("bool_key", true);
  ASSERT_EQ("bool_key", bool_val_bind_prop.key.data.str_key);
  ASSERT_TRUE(bool_val_bind_prop.value.data.bool_value);

  auto enum_val_bind_prop = ddk::BindPropertyEnum("enum_key", "fuchsia.gpio.BIND_PROTOCOL.DEVICE");
  ASSERT_EQ("enum_key", enum_val_bind_prop.key.data.str_key);
  ASSERT_EQ("fuchsia.gpio.BIND_PROTOCOL.DEVICE", enum_val_bind_prop.value.data.enum_value);
}

}  // namespace
