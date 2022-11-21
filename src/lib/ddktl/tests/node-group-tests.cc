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

void VerifyPropertyKey(device_bind_prop_key_t expected, device_bind_prop_key_t actual) {
  ASSERT_EQ(expected.key_type, actual.key_type);
  switch (expected.key_type) {
    case DEVICE_BIND_PROPERTY_KEY_INT: {
      ASSERT_EQ(expected.data.int_key, actual.data.int_key);
      break;
    }
    case DEVICE_BIND_PROPERTY_KEY_STRING: {
      ASSERT_STREQ(expected.data.str_key, actual.data.str_key);
      break;
    }
    default: {
      ASSERT_TRUE(false);
    }
  }
}

void VerifyPropertyValue(device_bind_prop_value_t expected, device_bind_prop_value_t actual) {
  ASSERT_EQ(expected.data_type, actual.data_type);
  switch (expected.data_type) {
    case ZX_DEVICE_PROPERTY_VALUE_INT: {
      ASSERT_EQ(expected.data.int_value, actual.data.int_value);
      break;
    }
    case ZX_DEVICE_PROPERTY_VALUE_STRING: {
      ASSERT_STREQ(expected.data.str_value, actual.data.str_value);
      break;
    }
    case ZX_DEVICE_PROPERTY_VALUE_BOOL: {
      ASSERT_EQ(expected.data.bool_value, actual.data.bool_value);
      break;
    }
    case ZX_DEVICE_PROPERTY_VALUE_ENUM: {
      ASSERT_STREQ(expected.data.enum_value, actual.data.enum_value);
      break;
    }
    default: {
      ASSERT_TRUE(false);
    }
  }
}

class NodeGroupTest : public zxtest::Test {};

TEST_F(NodeGroupTest, CreateAcceptBindRules) {
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
      ddk::MakeAcceptBindRule("enum_based_val", "fuchsia.gpio.BIND_PROTOCOL.DEVICE");
  ASSERT_STREQ("enum_based_val", enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(1, enum_val_bind_rule.get().values_count);
  ASSERT_STREQ("fuchsia.gpio.BIND_PROTOCOL.DEVICE",
               enum_val_bind_rule.get().values[0].data.enum_value);
}

TEST_F(NodeGroupTest, CreateAcceptBindRulesGeneratedConstants) {
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
      ddk::MakeAcceptBindRule(bind_testlib::ENUM_PROP, bind_testlib::ENUM_PROP_VALUE);
  ASSERT_STREQ(bind_testlib::ENUM_PROP, enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(1, enum_val_bind_rule.get().values_count);
  ASSERT_STREQ(bind_testlib::ENUM_PROP_VALUE, enum_val_bind_rule.get().values[0].data.enum_value);
}

TEST_F(NodeGroupTest, CreateRejectBindRules) {
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
      ddk::MakeRejectBindRule("enum_based_val", "fuchsia.gpio.BIND_PROTOCOL.DEVICE");
  ASSERT_STREQ("enum_based_val", enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(1, enum_val_bind_rule.get().values_count);
  ASSERT_STREQ("fuchsia.gpio.BIND_PROTOCOL.DEVICE",
               enum_val_bind_rule.get().values[0].data.enum_value);
}

TEST_F(NodeGroupTest, CreateRejectBindRulesGeneratedConstants) {
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
      ddk::MakeRejectBindRule(bind_testlib::ENUM_PROP, bind_testlib::ENUM_PROP_VALUE);
  ASSERT_STREQ(bind_testlib::ENUM_PROP, enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(1, enum_val_bind_rule.get().values_count);
  ASSERT_STREQ(bind_testlib::ENUM_PROP_VALUE, enum_val_bind_rule.get().values[0].data.enum_value);
}

TEST_F(NodeGroupTest, CreateAcceptBindRuleList) {
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
  auto enum_val_bind_rule = ddk::BindRuleAcceptList("enum_based_val", enum_val_bind_rule_values);
  ASSERT_STREQ("enum_based_val", enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(2, enum_val_bind_rule.get().values_count);
  ASSERT_STREQ("fuchsia.gpio.BIND_PROTOCOL.DEVICE",
               enum_val_bind_rule.get().values[0].data.enum_value);
  ASSERT_STREQ("fuchsia.gpio.BIND_PROTOCOL.IMPL",
               enum_val_bind_rule.get().values[1].data.enum_value);
}

TEST_F(NodeGroupTest, CreateAcceptBindRuleListWithConstants) {
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
      ddk::BindRuleAcceptList(bind_testlib::ENUM_PROP, enum_val_bind_rule_values);
  ASSERT_STREQ(bind_testlib::ENUM_PROP, enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(2, enum_val_bind_rule.get().values_count);
  ASSERT_STREQ(bind_testlib::ENUM_PROP_VALUE, enum_val_bind_rule.get().values[0].data.enum_value);
  ASSERT_STREQ(bind_testlib::ENUM_PROP_VALUE_2, enum_val_bind_rule.get().values[1].data.enum_value);
}

TEST_F(NodeGroupTest, CreateRejectBindRuleList) {
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

TEST_F(NodeGroupTest, CreateRejectBindRuleListWithConstants) {
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
      ddk::BindRuleRejectList(bind_testlib::ENUM_PROP, enum_val_bind_rule_values);
  ASSERT_STREQ(bind_testlib::ENUM_PROP, enum_val_bind_rule.get().key.data.str_key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_REJECT, enum_val_bind_rule.get().condition);
  ASSERT_EQ(2, enum_val_bind_rule.get().values_count);
  ASSERT_STREQ(bind_testlib::ENUM_PROP_VALUE, enum_val_bind_rule.get().values[0].data.enum_value);
  ASSERT_STREQ(bind_testlib::ENUM_PROP_VALUE_2, enum_val_bind_rule.get().values[1].data.enum_value);
}

TEST_F(NodeGroupTest, CreateBindProperties) {
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

TEST_F(NodeGroupTest, CreateBindPropertiesWithContants) {
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

TEST_F(NodeGroupTest, CreateNodeGroupDescValues) {
  const ddk::NodeGroupBindRule kBindRules[] = {
      ddk::MakeAcceptBindRule("test", static_cast<uint32_t>(10)),
  };

  const device_bind_prop_t kBindProperties[] = {
      ddk::MakeProperty("test", static_cast<uint32_t>(10)),
  };

  auto node_group_desc = ddk::NodeGroupDesc(kBindRules, kBindProperties);

  {
    auto dealloc_props = std::vector{
        ddk::MakeProperty("test", static_cast<uint32_t>(10)),
        ddk::MakeProperty("swallow", true),
    };

    // Store the int values dynacmically into a vector and then pass
    // it to |node_group_desc|.
    uint32_t kTestDeallocIntValues[] = {10, 20, 100};
    std::vector<ddk::NodeGroupBindRule> dealloc_rules;
    for (auto val : kTestDeallocIntValues) {
      dealloc_rules.push_back(ddk::MakeAcceptBindRule("test", val));
    }
    node_group_desc.AddNodeRepresentation(dealloc_rules, dealloc_props);
  }

  // Verifying the node group desc.
  auto desc = node_group_desc.get();
  ASSERT_EQ(2, desc.nodes_count);

  // Verify the bind properties in the first node.
  auto node_1 = desc.nodes[0];
  ASSERT_EQ(1, node_1.bind_property_count);
  VerifyPropertyKey(device_bind_prop_str_key("test"), node_1.bind_properties[0].key);
  VerifyPropertyValue(device_bind_prop_int_val(10), node_1.bind_properties[0].value);

  // Verify the bind rules in the first node.
  ASSERT_EQ(1, node_1.bind_rule_count);
  VerifyPropertyKey(device_bind_prop_str_key("test"), node_1.bind_rules[0].key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, node_1.bind_rules[0].condition);
  ASSERT_EQ(1, node_1.bind_rules[0].values_count);
  VerifyPropertyValue(device_bind_prop_int_val(10), node_1.bind_rules[0].values[0]);

  // Verify the bind properties in the second node.
  auto node_2 = desc.nodes[1];
  ASSERT_EQ(2, node_2.bind_property_count);
  VerifyPropertyKey(device_bind_prop_str_key("test"), node_2.bind_properties[0].key);
  VerifyPropertyValue(device_bind_prop_int_val(10), node_2.bind_properties[0].value);
  VerifyPropertyKey(device_bind_prop_str_key("swallow"), node_2.bind_properties[1].key);
  VerifyPropertyValue(device_bind_prop_bool_val(true), node_2.bind_properties[1].value);

  // Verify the bind rules in the second node.
  ASSERT_EQ(3, node_2.bind_rule_count);
  VerifyPropertyKey(device_bind_prop_str_key("test"), node_2.bind_rules[0].key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, node_2.bind_rules[0].condition);
  ASSERT_EQ(1, node_2.bind_rules[1].values_count);
  VerifyPropertyValue(device_bind_prop_int_val(10), node_2.bind_rules[0].values[0]);

  VerifyPropertyKey(device_bind_prop_str_key("test"), node_2.bind_rules[1].key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, node_2.bind_rules[1].condition);
  ASSERT_EQ(1, node_2.bind_rules[1].values_count);
  VerifyPropertyValue(device_bind_prop_int_val(20), node_2.bind_rules[1].values[0]);

  VerifyPropertyKey(device_bind_prop_str_key("test"), node_2.bind_rules[2].key);
  ASSERT_EQ(DEVICE_BIND_RULE_CONDITION_ACCEPT, node_2.bind_rules[2].condition);
  ASSERT_EQ(1, node_2.bind_rules[2].values_count);
  VerifyPropertyValue(device_bind_prop_int_val(100), node_2.bind_rules[2].values[0]);
}

}  // namespace
