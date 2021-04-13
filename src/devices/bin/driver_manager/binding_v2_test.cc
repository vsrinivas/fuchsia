// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/driver.h>

#include <zxtest/zxtest.h>

#include "coordinator.h"
#include "src/devices/lib/bind/ffi_bindings.h"

// Tests for matching device properties to the new bytecode.

bool match_bind_rules(const uint8_t* bytecode, const zx_device_prop_t* props, size_t bytecode_sz,
                      size_t props_sz, uint32_t protocol_id, bool autobind) {
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

  return driver_is_bindable(&driver, protocol_id, std::move(properties), autobind);
}

TEST(BindingV2Test, SingleAbortInstruction) {
  uint8_t bytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0, 0x0,  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x0,  0x0, 0x0, 0x0,  // Symbol table header
      0x49, 0x4E, 0x53, 0x54, 0x01, 0x0, 0x0, 0x0,  // Instruction header
      0x30                                          // Abort instruction
  };
  zx_device_prop_t properties[] = {zx_device_prop_t{5, 0, 2}};
  ASSERT_FALSE(
      match_bind_rules(bytecode, properties, std::size(bytecode), std::size(properties), 5, false));
}

TEST(BindingV2Test, NoBindRules) {
  uint8_t bytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0, 0x0,  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x0,  0x0, 0x0, 0x0,  // Symbol table header
      0x49, 0x4E, 0x53, 0x54, 0x0,  0x0, 0x0, 0x0   // Instruction header
  };
  zx_device_prop_t properties[] = {zx_device_prop_t{5, 0, 2}};
  ASSERT_TRUE(
      match_bind_rules(bytecode, properties, std::size(bytecode), std::size(properties), 5, false));
}

TEST(BindingV2Test, MatchDeviceProperty) {
  uint8_t bytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x0,  0x0, 0x0,  0x0,                  // Symbol table header
      0x49, 0x4E, 0x53, 0x54, 0x0B, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x01, 0x05, 0x0,  0x0,  0x0, 0x01, 0x02, 0x0, 0x0, 0x0,  // Equal instruction
  };
  zx_device_prop_t properties[] = {zx_device_prop_t{4, 0, 3}, zx_device_prop_t{5, 0, 2}};
  ASSERT_TRUE(
      match_bind_rules(bytecode, properties, std::size(bytecode), std::size(properties), 5, false));
}

TEST(BindingV2Test, MismatchDeviceProperty) {
  uint8_t bytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x0,  0x0, 0x0,  0x0,                  // Symbol table header
      0x49, 0x4E, 0x53, 0x54, 0x0B, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x01, 0x05, 0x0,  0x0,  0x0, 0x01, 0x02, 0x0, 0x0, 0x0,  // Equal instruction
  };
  zx_device_prop_t properties[] = {zx_device_prop_t{5, 0, 20}};
  ASSERT_FALSE(
      match_bind_rules(bytecode, properties, std::size(bytecode), std::size(properties), 5, false));
}

TEST(BindingV2Test, NoDevicePropertiesWithMismatchProtocolId) {
  uint8_t bytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x0,  0x0, 0x0,  0x0,                  // Symbol table header
      0x49, 0x4E, 0x53, 0x54, 0x0B, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x01, 0x01, 0x0,  0x0,  0x0, 0x01, 0x02, 0x0, 0x0, 0x0,  // Equal instruction
  };
  zx_device_prop_t properties[] = {};
  ASSERT_FALSE(match_bind_rules(bytecode, properties, std::size(bytecode), 0, 5, false));
}

TEST(BindingV2Test, NoDevicePropertiesWithMatchingProtocolId) {
  uint8_t bytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x0,  0x0, 0x0,  0x0,                  // Symbol table header
      0x49, 0x4E, 0x53, 0x54, 0x0B, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x01, 0x01, 0x0,  0x0,  0x0, 0x01, 0x05, 0x0, 0x0, 0x0,  // Equal instruction
  };
  zx_device_prop_t properties[] = {};
  ASSERT_TRUE(match_bind_rules(bytecode, properties, std::size(bytecode), 0, 5, false));
}

TEST(BindingV2Test, NoDevicePropertiesWithMismatchAutobind) {
  uint8_t bytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x0,  0x0, 0x0,  0x0,                  // Symbol table header
      0x49, 0x4E, 0x53, 0x54, 0x0B, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x01, 0x02, 0x0,  0x0,  0x0, 0x01, 0x01, 0x0, 0x0, 0x0,  //  Equal instruction
  };
  zx_device_prop_t properties[] = {};
  ASSERT_FALSE(match_bind_rules(bytecode, properties, std::size(bytecode), 0, 5, false));
}

TEST(BindingV2Test, NoDevicePropertiesWithMatchingAutobind) {
  uint8_t bytecode[] = {
      0x42, 0x49, 0x4E, 0x44, 0x02, 0x0, 0x0,  0x0,                  // Bind header
      0x53, 0x59, 0x4E, 0x42, 0x0,  0x0, 0x0,  0x0,                  // Symbol table header
      0x49, 0x4E, 0x53, 0x54, 0x0B, 0x0, 0x0,  0x0,                  // Instruction header
      0x01, 0x01, 0x02, 0x0,  0x0,  0x0, 0x01, 0x01, 0x0, 0x0, 0x0,  //  Equal instruction
  };
  zx_device_prop_t properties[] = {};
  ASSERT_TRUE(match_bind_rules(bytecode, properties, std::size(bytecode), 0, 5, true));
}

TEST(BindingV2Test, EmptyBytecode) {
  uint8_t bytecode[] = {};
  zx_device_prop_t properties[] = {zx_device_prop_t{5, 0, 20}};
  ASSERT_FALSE(match_bind_rules(bytecode, properties, 0, std::size(properties), 5, false));
}
