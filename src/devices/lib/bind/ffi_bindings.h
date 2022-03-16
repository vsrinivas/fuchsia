// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_BIND_FFI_BINDINGS_H_
#define SRC_DEVICES_LIB_BIND_FFI_BINDINGS_H_

#include <cstdint>
#include <cstdlib>

enum class ValueType : uint32_t {
  NumberVal = 0,
  StringVal = 1,
  BoolVal = 2,
  EnumVal = 3,
};

union value_t {
  uint32_t num_value;
  const char *str_value;
  bool bool_value;
};

struct property_value_t {
  ValueType val_type;
  value_t value;
};

// This struct should only be constructed via the below helper functions.
// This is to ensure that the fields are set correctly to avoid unsafe
// behavior.
struct device_str_property_t {
  const char *key;
  property_value_t value;
};

struct device_property_t {
  uint32_t key;
  uint32_t value;
};

extern "C" {

// Helper functions for constructing device_str_property_t.
device_str_property_t str_property_with_string(const char *key, const char *value);
device_str_property_t str_property_with_int(const char *key, uint32_t value);
device_str_property_t str_property_with_bool(const char *key, bool value);
device_str_property_t str_property_with_enum(const char *key, const char *value);

bool match_bind_rules(const uint8_t *bytecode_c, size_t bytecode_sz,
                      const device_property_t *properties_c, size_t properties_sz,
                      const device_str_property_t *str_properties_c, size_t str_properties_sz,
                      uint32_t protocol_id, bool autobind);

}  // extern "C"

#endif  // SRC_DEVICES_LIB_BIND_FFI_BINDINGS_H_
