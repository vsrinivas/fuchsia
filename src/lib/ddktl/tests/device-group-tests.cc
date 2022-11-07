// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/device.h>

#include <memory>

#include <bind/fuchsia/cpp/bind.h>
#include <bind/testlib/cpp/bind.h>
#include <ddktl/device.h>
#include <zxtest/zxtest.h>

namespace {

class DeviceGroupTest : public zxtest::Test {};

TEST_F(DeviceGroupTest, CreateAcceptBindRules) {
  auto int_key_bind_rule = ddk::MakeAcceptBindRule(5, 100);
  ASSERT_EQ(5, int_key_bind_rule.get().key.data.int_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, int_key_bind_rule.get().condition);
  ASSERT_EQ(1, int_key_bind_rule.get().values_count);
  ASSERT_EQ(100, int_key_bind_rule.get().values[0].data.int_value);

  auto int_val_bind_rule = ddk::MakeAcceptBindRule("int_based_val", static_cast<uint32_t>(50));
  ASSERT_STREQ("int_based_val", int_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, int_val_bind_rule.get().condition);
  ASSERT_EQ(1, int_val_bind_rule.get().values_count);
  ASSERT_EQ(50, int_val_bind_rule.get().values[0].data.int_value);

  auto str_val_bind_rule = ddk::MakeAcceptBindRule("string_based_val", "thrush");
  ASSERT_STREQ("string_based_val", str_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, str_val_bind_rule.get().condition);
  ASSERT_EQ(1, str_val_bind_rule.get().values_count);
  ASSERT_STREQ("thrush", str_val_bind_rule.get().values[0].data.str_value);

  auto bool_val_bind_rule = ddk::MakeAcceptBindRule("bool_based_val", true);
  ASSERT_STREQ("bool_based_val", bool_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, bool_val_bind_rule.get().condition);
  ASSERT_EQ(1, bool_val_bind_rule.get().values_count);
  ASSERT_TRUE(bool_val_bind_rule.get().values[0].data.bool_value);

  auto enum_val_bind_rule =
      ddk::MakeAcceptEnumBindRule("enum_based_val", "fuchsia.gpio.BIND_PROTOCOL.DEVICE");
  ASSERT_STREQ("enum_based_val", enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(1, enum_val_bind_rule.get().values_count);
  ASSERT_STREQ("fuchsia.gpio.BIND_PROTOCOL.DEVICE",
               enum_val_bind_rule.get().values[0].data.enum_value);
}

TEST_F(DeviceGroupTest, CreateAcceptBindRulesGeneratedConstants) {
  auto int_val_bind_rule =
      ddk::MakeAcceptBindRule(bind_fuchsia::PROTOCOL, bind_testlib::BIND_PROTOCOL_VALUE);
  ASSERT_STREQ(bind_fuchsia::PROTOCOL, int_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, int_val_bind_rule.get().condition);
  ASSERT_EQ(1, int_val_bind_rule.get().values_count);
  ASSERT_EQ(bind_testlib::BIND_PROTOCOL_VALUE, int_val_bind_rule.get().values[0].data.int_value);

  auto str_val_bind_rule =
      ddk::MakeAcceptBindRule(bind_testlib::STRING_PROP, bind_testlib::STRING_PROP_VALUE);
  ASSERT_STREQ(bind_testlib::STRING_PROP, str_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, str_val_bind_rule.get().condition);
  ASSERT_EQ(1, str_val_bind_rule.get().values_count);
  ASSERT_STREQ(bind_testlib::STRING_PROP_VALUE, str_val_bind_rule.get().values[0].data.str_value);

  auto bool_val_bind_rule =
      ddk::MakeAcceptBindRule(bind_testlib::BOOL_PROP, bind_testlib::BOOL_PROP_VALUE);
  ASSERT_STREQ(bind_testlib::BOOL_PROP, bool_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, bool_val_bind_rule.get().condition);
  ASSERT_EQ(1, bool_val_bind_rule.get().values_count);
  ASSERT_EQ(bind_testlib::BOOL_PROP_VALUE, bool_val_bind_rule.get().values[0].data.bool_value);

  auto enum_val_bind_rule =
      ddk::MakeAcceptEnumBindRule(bind_testlib::ENUM_PROP, bind_testlib::ENUM_PROP_VALUE);
  ASSERT_STREQ(bind_testlib::ENUM_PROP, enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(1, enum_val_bind_rule.get().values_count);
  ASSERT_STREQ(bind_testlib::ENUM_PROP_VALUE, enum_val_bind_rule.get().values[0].data.enum_value);
}

TEST_F(DeviceGroupTest, CreateRejectBindRules) {
  auto int_key_bind_rule = ddk::MakeRejectBindRule(5, 100);
  ASSERT_EQ(5, int_key_bind_rule.get().key.data.int_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, int_key_bind_rule.get().condition);
  ASSERT_EQ(1, int_key_bind_rule.get().values_count);
  ASSERT_EQ(100, int_key_bind_rule.get().values[0].data.int_value);

  auto int_val_bind_rule = ddk::MakeRejectBindRule("int_based_val", static_cast<uint32_t>(50));
  ASSERT_STREQ("int_based_val", int_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, int_val_bind_rule.get().condition);
  ASSERT_EQ(1, int_val_bind_rule.get().values_count);
  ASSERT_EQ(50, int_val_bind_rule.get().values[0].data.int_value);

  auto str_val_bind_rule = ddk::MakeRejectBindRule("string_based_val", "thrush");
  ASSERT_STREQ("string_based_val", str_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, str_val_bind_rule.get().condition);
  ASSERT_EQ(1, str_val_bind_rule.get().values_count);
  ASSERT_STREQ("thrush", str_val_bind_rule.get().values[0].data.str_value);

  auto bool_val_bind_rule = ddk::MakeRejectBindRule("bool_based_val", true);
  ASSERT_STREQ("bool_based_val", bool_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, bool_val_bind_rule.get().condition);
  ASSERT_EQ(1, bool_val_bind_rule.get().values_count);
  ASSERT_TRUE(bool_val_bind_rule.get().values[0].data.bool_value);

  auto enum_val_bind_rule =
      ddk::MakeRejectEnumBindRule("enum_based_val", "fuchsia.gpio.BIND_PROTOCOL.DEVICE");
  ASSERT_STREQ("enum_based_val", enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(1, enum_val_bind_rule.get().values_count);
  ASSERT_STREQ("fuchsia.gpio.BIND_PROTOCOL.DEVICE",
               enum_val_bind_rule.get().values[0].data.enum_value);
}

TEST_F(DeviceGroupTest, CreateRejectBindRulesGeneratedConstants) {
  auto int_val_bind_rule =
      ddk::MakeRejectBindRule(bind_fuchsia::PROTOCOL, bind_testlib::BIND_PROTOCOL_VALUE);
  ASSERT_STREQ(bind_fuchsia::PROTOCOL, int_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, int_val_bind_rule.get().condition);
  ASSERT_EQ(1, int_val_bind_rule.get().values_count);
  ASSERT_EQ(bind_testlib::BIND_PROTOCOL_VALUE, int_val_bind_rule.get().values[0].data.int_value);

  auto str_val_bind_rule =
      ddk::MakeRejectBindRule(bind_testlib::STRING_PROP, bind_testlib::STRING_PROP_VALUE);
  ASSERT_STREQ(bind_testlib::STRING_PROP, str_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, str_val_bind_rule.get().condition);
  ASSERT_EQ(1, str_val_bind_rule.get().values_count);
  ASSERT_STREQ(bind_testlib::STRING_PROP_VALUE, str_val_bind_rule.get().values[0].data.str_value);

  auto bool_val_bind_rule =
      ddk::MakeRejectBindRule(bind_testlib::BOOL_PROP, bind_testlib::BOOL_PROP_VALUE);
  ASSERT_STREQ(bind_testlib::BOOL_PROP, bool_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, bool_val_bind_rule.get().condition);
  ASSERT_EQ(1, bool_val_bind_rule.get().values_count);
  ASSERT_EQ(bind_testlib::BOOL_PROP_VALUE, bool_val_bind_rule.get().values[0].data.bool_value);

  auto enum_val_bind_rule =
      ddk::MakeRejectEnumBindRule(bind_testlib::ENUM_PROP, bind_testlib::ENUM_PROP_VALUE);
  ASSERT_STREQ(bind_testlib::ENUM_PROP, enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(1, enum_val_bind_rule.get().values_count);
  ASSERT_STREQ(bind_testlib::ENUM_PROP_VALUE, enum_val_bind_rule.get().values[0].data.enum_value);
}

TEST_F(DeviceGroupTest, CreateAcceptBindRuleList) {
  const uint32_t int_key_bind_rule_values[] = {10, 3};
  auto int_key_bind_rule = ddk::BindRuleAcceptList(5, int_key_bind_rule_values);
  ASSERT_EQ(5, int_key_bind_rule.get().key.data.int_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, int_key_bind_rule.get().condition);
  ASSERT_EQ(2, int_key_bind_rule.get().values_count);
  ASSERT_EQ(10, int_key_bind_rule.get().values[0].data.int_value);
  ASSERT_EQ(3, int_key_bind_rule.get().values[1].data.int_value);

  const uint32_t int_val_bind_rule_values[] = {20, 150, 8};
  auto int_val_bind_rule = ddk::BindRuleAcceptList("int_based_val", int_val_bind_rule_values);
  ASSERT_STREQ("int_based_val", int_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, int_val_bind_rule.get().condition);
  ASSERT_EQ(3, int_val_bind_rule.get().values_count);
  ASSERT_EQ(20, int_val_bind_rule.get().values[0].data.int_value);
  ASSERT_EQ(150, int_val_bind_rule.get().values[1].data.int_value);
  ASSERT_EQ(8, int_val_bind_rule.get().values[2].data.int_value);

  const char* str_val_bind_rule_values[] = {"thrush", "robin"};
  auto str_val_bind_rule = ddk::BindRuleAcceptList("string_based_val", str_val_bind_rule_values);
  ASSERT_STREQ("string_based_val", str_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, str_val_bind_rule.get().condition);
  ASSERT_EQ(2, str_val_bind_rule.get().values_count);
  ASSERT_STREQ("thrush", str_val_bind_rule.get().values[0].data.str_value);
  ASSERT_STREQ("robin", str_val_bind_rule.get().values[1].data.str_value);

  const char* enum_val_bind_rule_values[] = {"fuchsia.gpio.BIND_PROTOCOL.DEVICE",
                                             "fuchsia.gpio.BIND_PROTOCOL.IMPL"};
  auto enum_val_bind_rule =
      ddk::MakeAcceptEnumBindRuleList("enum_based_val", enum_val_bind_rule_values);
  ASSERT_STREQ("enum_based_val", enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(2, enum_val_bind_rule.get().values_count);
  ASSERT_STREQ("fuchsia.gpio.BIND_PROTOCOL.DEVICE",
               enum_val_bind_rule.get().values[0].data.enum_value);
  ASSERT_STREQ("fuchsia.gpio.BIND_PROTOCOL.IMPL",
               enum_val_bind_rule.get().values[1].data.enum_value);
}

TEST_F(DeviceGroupTest, CreateAcceptBindRuleListWithConstants) {
  const uint32_t int_val_bind_rule_values[] = {bind_testlib::BIND_PROTOCOL_VALUE,
                                               bind_testlib::BIND_PROTOCOL_VALUE_2};
  auto int_val_bind_rule =
      ddk::BindRuleAcceptList(bind_fuchsia::PROTOCOL, int_val_bind_rule_values);
  ASSERT_STREQ(bind_fuchsia::PROTOCOL, int_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, int_val_bind_rule.get().condition);
  ASSERT_EQ(2, int_val_bind_rule.get().values_count);
  ASSERT_EQ(bind_testlib::BIND_PROTOCOL_VALUE, int_val_bind_rule.get().values[0].data.int_value);
  ASSERT_EQ(bind_testlib::BIND_PROTOCOL_VALUE_2, int_val_bind_rule.get().values[1].data.int_value);

  const char* str_val_bind_rule_values[] = {bind_testlib::STRING_PROP_VALUE.c_str(),
                                            bind_testlib::STRING_PROP_VALUE_2.c_str()};
  auto str_val_bind_rule =
      ddk::BindRuleAcceptList(bind_testlib::STRING_PROP, str_val_bind_rule_values);
  ASSERT_STREQ(bind_testlib::STRING_PROP, str_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, str_val_bind_rule.get().condition);
  ASSERT_EQ(2, str_val_bind_rule.get().values_count);
  ASSERT_STREQ(bind_testlib::STRING_PROP_VALUE, str_val_bind_rule.get().values[0].data.str_value);
  ASSERT_STREQ(bind_testlib::STRING_PROP_VALUE_2, str_val_bind_rule.get().values[1].data.str_value);

  const char* enum_val_bind_rule_values[] = {bind_testlib::ENUM_PROP_VALUE.c_str(),
                                             bind_testlib::ENUM_PROP_VALUE_2.c_str()};
  auto enum_val_bind_rule =
      ddk::MakeAcceptEnumBindRuleList(bind_testlib::ENUM_PROP, enum_val_bind_rule_values);
  ASSERT_STREQ(bind_testlib::ENUM_PROP, enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(2, enum_val_bind_rule.get().values_count);
  ASSERT_STREQ(bind_testlib::ENUM_PROP_VALUE, enum_val_bind_rule.get().values[0].data.enum_value);
  ASSERT_STREQ(bind_testlib::ENUM_PROP_VALUE_2, enum_val_bind_rule.get().values[1].data.enum_value);
}

TEST_F(DeviceGroupTest, CreateRejectBindRuleList) {
  const uint32_t int_key_bind_rule_values[] = {10, 3};
  auto int_key_bind_rule = ddk::BindRuleRejectList(5, int_key_bind_rule_values);
  ASSERT_EQ(5, int_key_bind_rule.get().key.data.int_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, int_key_bind_rule.get().condition);
  ASSERT_EQ(2, int_key_bind_rule.get().values_count);
  ASSERT_EQ(10, int_key_bind_rule.get().values[0].data.int_value);
  ASSERT_EQ(3, int_key_bind_rule.get().values[1].data.int_value);

  const uint32_t int_val_bind_rule_values[] = {20, 150, 8};
  auto int_val_bind_rule = ddk::BindRuleRejectList("int_based_val", int_val_bind_rule_values);
  ASSERT_STREQ("int_based_val", int_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, int_val_bind_rule.get().condition);
  ASSERT_EQ(3, int_val_bind_rule.get().values_count);
  ASSERT_EQ(20, int_val_bind_rule.get().values[0].data.int_value);
  ASSERT_EQ(150, int_val_bind_rule.get().values[1].data.int_value);
  ASSERT_EQ(8, int_val_bind_rule.get().values[2].data.int_value);

  const char* str_val_bind_rule_values[] = {"thrush", "robin"};
  auto str_val_bind_rule = ddk::BindRuleRejectList("string_based_val", str_val_bind_rule_values);
  ASSERT_STREQ("string_based_val", str_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, str_val_bind_rule.get().condition);
  ASSERT_EQ(2, str_val_bind_rule.get().values_count);
  ASSERT_STREQ("thrush", str_val_bind_rule.get().values[0].data.str_value);
  ASSERT_STREQ("robin", str_val_bind_rule.get().values[1].data.str_value);

  const char* enum_val_bind_rule_values[] = {"fuchsia.gpio.BIND_PROTOCOL.DEVICE",
                                             "fuchsia.gpio.BIND_PROTOCOL.IMPL"};
  auto enum_val_bind_rule = ddk::BindRuleRejectList("enum_based_val", enum_val_bind_rule_values);
  ASSERT_STREQ("enum_based_val", enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(2, enum_val_bind_rule.get().values_count);
  ASSERT_STREQ("fuchsia.gpio.BIND_PROTOCOL.DEVICE",
               enum_val_bind_rule.get().values[0].data.enum_value);
  ASSERT_STREQ("fuchsia.gpio.BIND_PROTOCOL.IMPL",
               enum_val_bind_rule.get().values[1].data.enum_value);
}

TEST_F(DeviceGroupTest, CreateRejectBindRuleListWithConstants) {
  const uint32_t int_val_bind_rule_values[] = {bind_testlib::BIND_PROTOCOL_VALUE,
                                               bind_testlib::BIND_PROTOCOL_VALUE_2};
  auto int_val_bind_rule =
      ddk::BindRuleRejectList(bind_fuchsia::PROTOCOL, int_val_bind_rule_values);
  ASSERT_STREQ(bind_fuchsia::PROTOCOL, int_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, int_val_bind_rule.get().condition);
  ASSERT_EQ(2, int_val_bind_rule.get().values_count);
  ASSERT_EQ(bind_testlib::BIND_PROTOCOL_VALUE, int_val_bind_rule.get().values[0].data.int_value);
  ASSERT_EQ(bind_testlib::BIND_PROTOCOL_VALUE_2, int_val_bind_rule.get().values[1].data.int_value);

  const char* str_val_bind_rule_values[] = {bind_testlib::STRING_PROP_VALUE.c_str(),
                                            bind_testlib::STRING_PROP_VALUE_2.c_str()};
  auto str_val_bind_rule =
      ddk::BindRuleRejectList(bind_testlib::STRING_PROP, str_val_bind_rule_values);
  ASSERT_STREQ(bind_testlib::STRING_PROP, str_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, str_val_bind_rule.get().condition);
  ASSERT_EQ(2, str_val_bind_rule.get().values_count);
  ASSERT_STREQ(bind_testlib::STRING_PROP_VALUE, str_val_bind_rule.get().values[0].data.str_value);
  ASSERT_STREQ(bind_testlib::STRING_PROP_VALUE_2, str_val_bind_rule.get().values[1].data.str_value);

  const char* enum_val_bind_rule_values[] = {bind_testlib::ENUM_PROP_VALUE.c_str(),
                                             bind_testlib::ENUM_PROP_VALUE_2.c_str()};
  auto enum_val_bind_rule =
      ddk::MakeRejectEnumBindRuleList(bind_testlib::ENUM_PROP, enum_val_bind_rule_values);
  ASSERT_STREQ(bind_testlib::ENUM_PROP, enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(2, enum_val_bind_rule.get().values_count);
  ASSERT_STREQ(bind_testlib::ENUM_PROP_VALUE, enum_val_bind_rule.get().values[0].data.enum_value);
  ASSERT_STREQ(bind_testlib::ENUM_PROP_VALUE_2, enum_val_bind_rule.get().values[1].data.enum_value);
}

TEST_F(DeviceGroupTest, CreateBindProperties) {
  auto int_key_bind_prop = ddk::MakeProperty(1, 100);
  ASSERT_EQ(1, int_key_bind_prop.key.data.int_key);
  ASSERT_EQ(100, int_key_bind_prop.value.data.int_value);

  auto int_val_bind_prop = ddk::MakeProperty("int_key", static_cast<uint32_t>(20));
  ASSERT_STREQ("int_key", int_val_bind_prop.key.data.str_key);
  ASSERT_EQ(20, int_val_bind_prop.value.data.int_value);

  auto str_val_bind_prop = ddk::MakeProperty("str_key", "thrush");
  ASSERT_STREQ("str_key", str_val_bind_prop.key.data.str_key);
  ASSERT_STREQ("thrush", str_val_bind_prop.value.data.str_value);

  auto bool_val_bind_prop = ddk::MakeProperty("bool_key", true);
  ASSERT_STREQ("bool_key", bool_val_bind_prop.key.data.str_key);
  ASSERT_TRUE(bool_val_bind_prop.value.data.bool_value);

  auto enum_val_bind_prop = ddk::MakeProperty("enum_key", "fuchsia.gpio.BIND_PROTOCOL.DEVICE");
  ASSERT_STREQ("enum_key", enum_val_bind_prop.key.data.str_key);
  ASSERT_STREQ("fuchsia.gpio.BIND_PROTOCOL.DEVICE", enum_val_bind_prop.value.data.enum_value);
}

TEST_F(DeviceGroupTest, CreateBindPropertiesWithContants) {
  auto int_val_bind_prop =
      ddk::MakeProperty(bind_fuchsia::PROTOCOL, bind_testlib::BIND_PROTOCOL_VALUE);
  ASSERT_STREQ(bind_fuchsia::PROTOCOL, int_val_bind_prop.key.data.str_key);
  ASSERT_EQ(bind_testlib::BIND_PROTOCOL_VALUE, int_val_bind_prop.value.data.int_value);

  auto str_val_bind_prop =
      ddk::MakeProperty(bind_testlib::STRING_PROP, bind_testlib::STRING_PROP_VALUE);
  ASSERT_STREQ(bind_testlib::STRING_PROP, str_val_bind_prop.key.data.str_key);
  ASSERT_STREQ(bind_testlib::STRING_PROP_VALUE, str_val_bind_prop.value.data.str_value);

  auto bool_val_bind_prop =
      ddk::MakeProperty(bind_testlib::BOOL_PROP, bind_testlib::BOOL_PROP_VALUE);
  ASSERT_STREQ(bind_testlib::BOOL_PROP, bool_val_bind_prop.key.data.str_key);
  ASSERT_EQ(bind_testlib::BOOL_PROP_VALUE, bool_val_bind_prop.value.data.bool_value);

  auto enum_val_bind_prop =
      ddk::MakeProperty(bind_testlib::ENUM_PROP, bind_testlib::ENUM_PROP_VALUE);
  ASSERT_STREQ(bind_testlib::ENUM_PROP, enum_val_bind_prop.key.data.str_key);
  ASSERT_STREQ(bind_testlib::ENUM_PROP_VALUE, enum_val_bind_prop.value.data.enum_value);
}

}  // namespace
