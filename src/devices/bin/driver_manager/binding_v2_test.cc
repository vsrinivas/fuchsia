// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/driver.h>

#include <zxtest/zxtest.h>

#include "coordinator.h"
#include "src/devices/lib/bind/ffi_bindings.h"

// Tests for matching device properties to the new bytecode.

bool match_bind_rules(const uint8_t* bytecode, const zx_device_prop_t* props,
                      const StrProperty* str_props, size_t bytecode_sz, size_t props_sz,
                      size_t str_props_sz, uint32_t protocol_id, bool autobind) {
  auto driver = Driver();
  driver.bytecode_version = 2;

  driver.binding_size = bytecode_sz;
  auto bytecode_arr = std::make_unique<uint8_t[]>(bytecode_sz);
  if (bytecode) {
    memcpy(bytecode_arr.get(), bytecode, bytecode_sz * sizeof(bytecode[0]));
  }
  driver.binding = std::unique_ptr<uint8_t[]>(bytecode_arr.release());

  fbl::Array<zx_device_prop_t> properties(new zx_device_prop_t[props_sz], props_sz);
  if (props) {
    memcpy(properties.data(), props, sizeof(props[0]) * props_sz);
  }

  fbl::Array<StrProperty> str_properties(new StrProperty[str_props_sz], str_props_sz);
  if (str_props) {
    memcpy(str_properties.data(), str_props, sizeof(str_props[0]) * str_props_sz);
  }

  return can_driver_bind(&driver, protocol_id, std::move(properties), std::move(str_properties),
                         autobind);
}

TEST(BindingV2Test, SingleAbortInstruction) {
  const uint8_t kBytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0, 0x0,  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x0,  0x0, 0x0, 0x0,  // Symbol table header
      0x49, 0x4E, 0x53, 0x54, 0x01, 0x0, 0x0, 0x0,  // Instruction header
      0x30                                          // Abort instruction
  };
  const zx_device_prop_t kProperties[] = {zx_device_prop_t{5, 0, 2}};
  const StrProperty kStrProperties[] = {};
  ASSERT_FALSE(match_bind_rules(kBytecode, kProperties, kStrProperties, std::size(kBytecode),
                                std::size(kProperties), 0, 5, false));
}

TEST(BindingV2Test, NoBindRules) {
  const uint8_t kBytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0, 0x0,  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x0,  0x0, 0x0, 0x0,  // Symbol table header
      0x49, 0x4E, 0x53, 0x54, 0x0,  0x0, 0x0, 0x0   // Instruction header
  };
  const zx_device_prop_t kProperties[] = {zx_device_prop_t{5, 0, 2}};
  const StrProperty kStrProperties[] = {};
  ASSERT_TRUE(match_bind_rules(kBytecode, kProperties, kStrProperties, std::size(kBytecode),
                               std::size(kProperties), 0, 5, false));
}

TEST(BindingV2Test, MatchDeviceProperty) {
  const uint8_t kBytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x0,  0x0, 0x0,  0x0,                  // Symbol table header
      0x49, 0x4E, 0x53, 0x54, 0x0B, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x01, 0x05, 0x0,  0x0,  0x0, 0x01, 0x02, 0x0, 0x0, 0x0,  // Equal instruction
  };
  const zx_device_prop_t kProperties[] = {zx_device_prop_t{4, 0, 3}, zx_device_prop_t{5, 0, 2}};
  const StrProperty kStrProperties[] = {};
  ASSERT_TRUE(match_bind_rules(kBytecode, kProperties, kStrProperties, std::size(kBytecode),
                               std::size(kProperties), 0, 5, false));
}

TEST(BindingV2Test, MismatchDeviceProperty) {
  const uint8_t kBytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x0,  0x0, 0x0,  0x0,                  // Symbol table header
      0x49, 0x4E, 0x53, 0x54, 0x0B, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x01, 0x05, 0x0,  0x0,  0x0, 0x01, 0x02, 0x0, 0x0, 0x0,  // Equal instruction
  };
  const zx_device_prop_t kProperties[] = {zx_device_prop_t{5, 0, 20}};
  const StrProperty kStrProperties[] = {};
  ASSERT_FALSE(match_bind_rules(kBytecode, kProperties, kStrProperties, std::size(kBytecode),
                                std::size(kProperties), 0, 5, false));
}

TEST(BindingV2Test, NoDevicePropertiesWithMismatchProtocolId) {
  const uint8_t kBytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x0,  0x0, 0x0,  0x0,                  // Symbol table header
      0x49, 0x4E, 0x53, 0x54, 0x0B, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x01, 0x01, 0x0,  0x0,  0x0, 0x01, 0x02, 0x0, 0x0, 0x0,  // Equal instruction
  };
  const zx_device_prop_t kProperties[] = {};
  const StrProperty kStrProperties[] = {};
  ASSERT_FALSE(match_bind_rules(kBytecode, kProperties, kStrProperties, std::size(kBytecode), 0, 0,
                                5, false));
}

TEST(BindingV2Test, NoDevicePropertiesWithMatchingProtocolId) {
  const uint8_t kBytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x0,  0x0, 0x0,  0x0,                  // Symbol table header
      0x49, 0x4E, 0x53, 0x54, 0x0B, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x01, 0x01, 0x0,  0x0,  0x0, 0x01, 0x05, 0x0, 0x0, 0x0,  // Equal instruction
  };
  const zx_device_prop_t kProperties[] = {};
  const StrProperty kStrProperties[] = {};
  ASSERT_TRUE(match_bind_rules(kBytecode, kProperties, kStrProperties, std::size(kBytecode), 0, 0,
                               5, false));
}

TEST(BindingV2Test, NoDevicePropertiesWithMismatchAutobind) {
  const uint8_t kBytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x0,  0x0, 0x0,  0x0,                  // Symbol table header
      0x49, 0x4E, 0x53, 0x54, 0x0B, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x01, 0x02, 0x0,  0x0,  0x0, 0x01, 0x01, 0x0, 0x0, 0x0,  //  Equal instruction
  };
  const zx_device_prop_t kProperties[] = {};
  const StrProperty kStrProperties[] = {};
  ASSERT_FALSE(match_bind_rules(kBytecode, kProperties, kStrProperties, std::size(kBytecode), 0, 0,
                                5, false));
}

TEST(BindingV2Test, NoDevicePropertiesWithMatchingAutobind) {
  const uint8_t kBytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x0,  0x0, 0x0,  0x0,                  // Symbol table header
      0x49, 0x4E, 0x53, 0x54, 0x0B, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x01, 0x02, 0x0,  0x0,  0x0, 0x01, 0x01, 0x0, 0x0, 0x0,  //  Equal instruction
  };
  const zx_device_prop_t kProperties[] = {};
  const StrProperty kStrProperties[] = {};
  ASSERT_TRUE(match_bind_rules(kBytecode, kProperties, kStrProperties, std::size(kBytecode), 0, 0,
                               5, true));
}

TEST(BindingV2Test, MatchDeviceStringProperty) {
  const uint8_t kBytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x24, 0x0, 0x0,  0x0,                  // Symbol table header
      0x01, 0x0,  0x0,  0x0,                                         // "rail" symbol key (1)
      0x72, 0x61, 0x69, 0x6c, 0x0,                                   // "rail" string literal
      0x02, 0x0,  0x0,  0x0,                                         // "ruff" symbol key (2)
      0x72, 0x75, 0x66, 0x66, 0x0,                                   // "ruff" string literal
      0x03, 0x0,  0x0,  0x0,                                         // "coot" symbol key (3)
      0x63, 0x6F, 0x6F, 0x74, 0x0,                                   // "coot" string literal
      0x04, 0x0,  0x0,  0x0,                                         // "ibis" symbol key (4)
      0x69, 0x62, 0x69, 0x73, 0x0,                                   // "ibis" string literal
      0x49, 0x4E, 0x53, 0x54, 0x21, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x0,  0x01, 0x0,  0x0,  0x0, 0x02, 0x02, 0x0, 0x0, 0x0,  // "rail" == "ruff"
      0x01, 0x0,  0x04, 0x0,  0x0,  0x0, 0x03, 0x01, 0x0, 0x0, 0x0,  // "ibis" == true
      0x01, 0x0,  0x03, 0x0,  0x0,  0x0, 0x01, 0x08, 0x0, 0x0, 0x0,  // "coot" == 8
  };

  const zx_device_prop_t kProperties[] = {zx_device_prop_t{5, 0, 2}};
  const StrProperty kStrProperties[] = {
      StrProperty{.key = "woodpecker", .value = "sapsucker"},
      StrProperty{.key = "rail", .value = "ruff"},
      StrProperty{.key = "coot", .value = static_cast<uint32_t>(8)},
      StrProperty{.key = "ibis", .value = true},
  };

  ASSERT_TRUE(match_bind_rules(kBytecode, kProperties, kStrProperties, std::size(kBytecode),
                               std::size(kProperties), std::size(kStrProperties), 5, false));
}

TEST(BindingV2Test, MismatchDeviceStringPropertyWStringValue) {
  const uint8_t kBytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x12, 0x0, 0x0,  0x0,                  // Symbol table header
      0x01, 0x0,  0x0,  0x0,                                         // "rail" symbol key (1)
      0x72, 0x61, 0x69, 0x6c, 0x0,                                   // "rail" string literal
      0x02, 0x0,  0x0,  0x0,                                         // "ruff" symbol key (2)
      0x72, 0x75, 0x66, 0x66, 0x0,                                   // "ruff" string literal
      0x49, 0x4E, 0x53, 0x54, 0x0B, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x0,  0x01, 0x0,  0x0,  0x0, 0x02, 0x02, 0x0, 0x0, 0x0,  // Equal instruction
  };

  const zx_device_prop_t kProperties[] = {zx_device_prop_t{5, 0, 2}};
  const StrProperty kStrProperties[] = {StrProperty{.key = "rail", .value = "coot"}};

  ASSERT_FALSE(match_bind_rules(kBytecode, kProperties, kStrProperties, std::size(kBytecode),
                                std::size(kProperties), std::size(kStrProperties), 5, false));
}

TEST(BindingV2Test, MismatchDeviceStringPropertyWIntValue) {
  const uint8_t kBytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x12, 0x0, 0x0,  0x0,                  // Symbol table header
      0x01, 0x0,  0x0,  0x0,                                         // "rail" symbol key (1)
      0x72, 0x61, 0x69, 0x6c, 0x0,                                   // "rail" string literal
      0x02, 0x0,  0x0,  0x0,                                         // "ruff" symbol key (2)
      0x72, 0x75, 0x66, 0x66, 0x0,                                   // "ruff" string literal
      0x49, 0x4E, 0x53, 0x54, 0x0B, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x0,  0x01, 0x0,  0x0,  0x0, 0x01, 0x08, 0x0, 0x0, 0x0,  // "rail" == 8
  };

  const zx_device_prop_t kProperties[] = {zx_device_prop_t{5, 0, 2}};
  const StrProperty kStrProperties[] = {
      StrProperty{.key = "rail", .value = static_cast<uint32_t>(5)}};

  ASSERT_FALSE(match_bind_rules(kBytecode, kProperties, kStrProperties, std::size(kBytecode),
                                std::size(kProperties), std::size(kStrProperties), 5, false));
}

TEST(BindingV2Test, MismatchDeviceStringPropertyWBoolValue) {
  const uint8_t kBytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x12, 0x0, 0x0,  0x0,                  // Symbol table header
      0x01, 0x0,  0x0,  0x0,                                         // "rail" symbol key (1)
      0x72, 0x61, 0x69, 0x6c, 0x0,                                   // "rail" string literal
      0x02, 0x0,  0x0,  0x0,                                         // "ruff" symbol key (2)
      0x72, 0x75, 0x66, 0x66, 0x0,                                   // "ruff" string literal
      0x49, 0x4E, 0x53, 0x54, 0x0B, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x0,  0x01, 0x0,  0x0,  0x0, 0x03, 0x08, 0x0, 0x0, 0x0,  // "ruff" == true
  };

  const zx_device_prop_t kProperties[] = {zx_device_prop_t{5, 0, 2}};
  const StrProperty kStrProperties[] = {StrProperty{.key = "ruff", .value = false}};

  ASSERT_FALSE(match_bind_rules(kBytecode, kProperties, kStrProperties, std::size(kBytecode),
                                std::size(kProperties), std::size(kStrProperties), 5, false));
}

TEST(BindingV2Test, MatchDevicePropertyAndStringProperty) {
  const uint8_t kBytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x24, 0x0, 0x0,  0x0,                  // Symbol table header
      0x01, 0x0,  0x0,  0x0,                                         // "rail" symbol key (1)
      0x72, 0x61, 0x69, 0x6c, 0x0,                                   // "rail" string literal
      0x02, 0x0,  0x0,  0x0,                                         // "ruff" symbol key (2)
      0x72, 0x75, 0x66, 0x66, 0x0,                                   // "ruff" string literal
      0x03, 0x0,  0x0,  0x0,                                         // "coot" symbol key (3)
      0x63, 0x6F, 0x6F, 0x74, 0x0,                                   // "coot" string literal
      0x04, 0x0,  0x0,  0x0,                                         // "ibis" symbol key (4)
      0x69, 0x62, 0x69, 0x73, 0x0,                                   // "ibis" string literal
      0x49, 0x4E, 0x53, 0x54, 0x2C, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x0,  0x01, 0x0,  0x0,  0x0, 0x02, 0x02, 0x0, 0x0, 0x0,  // "rail" == "ruff"
      0x01, 0x01, 0x05, 0x0,  0x0,  0x0, 0x01, 0x02, 0x0, 0x0, 0x0,  // 5 == 2
      0x01, 0x0,  0x04, 0x0,  0x0,  0x0, 0x03, 0x01, 0x0, 0x0, 0x0,  // "ibis" == true
      0x01, 0x0,  0x03, 0x0,  0x0,  0x0, 0x01, 0x08, 0x0, 0x0, 0x0,  // "coot" == 8
  };

  const zx_device_prop_t kProperties[] = {zx_device_prop_t{5, 0, 2}};
  const StrProperty kStrProperties[] = {
      StrProperty{.key = "woodpecker", .value = "sapsucker"},
      StrProperty{.key = "rail", .value = "ruff"},
      StrProperty{.key = "coot", .value = static_cast<uint32_t>(8)},
      StrProperty{.key = "ibis", .value = true},
  };
  ASSERT_TRUE(match_bind_rules(kBytecode, kProperties, kStrProperties, std::size(kBytecode),
                               std::size(kProperties), std::size(kStrProperties), 5, false));
}

TEST(BindingV2Test, MatchDevicePropertyMismatchStringProperty) {
  const uint8_t kBytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x12, 0x0, 0x0,  0x0,                  // Symbol table header
      0x01, 0x0,  0x0,  0x0,                                         // "rail" symbol key (1)
      0x72, 0x61, 0x69, 0x6c, 0x0,                                   // "rail" string literal
      0x02, 0x0,  0x0,  0x0,                                         // "ruff" symbol key (2)
      0x72, 0x75, 0x66, 0x66, 0x0,                                   // "ruff" string literal
      0x49, 0x4E, 0x53, 0x54, 0x16, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x0,  0x01, 0x0,  0x0,  0x0, 0x02, 0x02, 0x0, 0x0, 0x0,  // "rail" == "ruff"
      0x01, 0x01, 0x05, 0x0,  0x0,  0x0, 0x01, 0x02, 0x0, 0x0, 0x0,  // 5 == 2
  };

  const zx_device_prop_t kProperties[] = {zx_device_prop_t{5, 0, 2}};
  const StrProperty kStrProperties[] = {StrProperty{.key = "rail", .value = "coot"}};

  ASSERT_FALSE(match_bind_rules(kBytecode, kProperties, kStrProperties, std::size(kBytecode),
                                std::size(kProperties), std::size(kStrProperties), 5, false));
}

TEST(BindingV2Test, MismatchDevicePropertyMatchStringProperty) {
  const uint8_t kBytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x12, 0x0, 0x0,  0x0,                  // Symbol table header
      0x01, 0x0,  0x0,  0x0,                                         // "rail" symbol key (1)
      0x72, 0x61, 0x69, 0x6c, 0x0,                                   // "rail" string literal
      0x02, 0x0,  0x0,  0x0,                                         // "ruff" symbol key (2)
      0x72, 0x75, 0x66, 0x66, 0x0,                                   // "ruff" string literal
      0x49, 0x4E, 0x53, 0x54, 0x16, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x0,  0x01, 0x0,  0x0,  0x0, 0x02, 0x02, 0x0, 0x0, 0x0,  // "rail" == "ruff"
      0x01, 0x01, 0x05, 0x0,  0x0,  0x0, 0x01, 0x02, 0x0, 0x0, 0x0,  // 5 == 2
  };

  const zx_device_prop_t kProperties[] = {zx_device_prop_t{5, 0, 3}};
  const StrProperty kStrProperties[] = {StrProperty{.key = "rail", .value = "ruff"}};

  ASSERT_FALSE(match_bind_rules(kBytecode, kProperties, kStrProperties, std::size(kBytecode),
                                std::size(kProperties), std::size(kStrProperties), 5, false));
}

TEST(BindingV2Test, StringPropertyNotInUnicode) {
  const uint8_t kBytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x0,  0x0, 0x0,  0x0,                  // Symbol table header
      0x49, 0x4E, 0x53, 0x54, 0x0B, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x01, 0x05, 0x0,  0x0,  0x0, 0x01, 0x02, 0x0, 0x0, 0x0,  // 5 == 2
  };

  // The device properties match the equal instruction.
  const zx_device_prop_t kProperties[] = {zx_device_prop_t{5, 0, 2}};

  // String properties containing invalid unicode characters in the key.
  const char kInvalidKey[] = {static_cast<char>(0xC0), 0};
  const StrProperty kInvalidStrProperties[] = {
      StrProperty{.key = kInvalidKey, .value = "honeyeater"}};
  ASSERT_FALSE(match_bind_rules(kBytecode, kProperties, kInvalidStrProperties, std::size(kBytecode),
                                std::size(kProperties), std::size(kInvalidStrProperties), 5,
                                false));

  // String properties containing invalid unicode characters in the value.
  const char kInvalidValue[] = {static_cast<char>(0xFF), 0};
  const StrProperty kInvalidStrProperties2[] = {
      StrProperty{.key = "wattlebird", .value = kInvalidValue}};
  ASSERT_FALSE(match_bind_rules(kBytecode, kProperties, kInvalidStrProperties2,
                                std::size(kBytecode), std::size(kProperties),
                                std::size(kInvalidStrProperties2), 5, false));

  // String properties containing invalid unicode characters in the key and value.
  const StrProperty kInvalidStrProperties3[] = {
      StrProperty{.key = kInvalidKey, .value = kInvalidValue}};
  ASSERT_FALSE(match_bind_rules(kBytecode, kProperties, kInvalidStrProperties3,
                                std::size(kBytecode), std::size(kProperties),
                                std::size(kInvalidStrProperties3), 5, false));
}

TEST(BindingV2Test, EmptyBytecode) {
  const uint8_t kBytecode[] = {};
  const zx_device_prop_t kProperties[] = {zx_device_prop_t{5, 0, 20}};
  const StrProperty kStrProperties[] = {};
  ASSERT_FALSE(match_bind_rules(kBytecode, kProperties, kStrProperties, 0, std::size(kProperties),
                                0, 5, false));
}
