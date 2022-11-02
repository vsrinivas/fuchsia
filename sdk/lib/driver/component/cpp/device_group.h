// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_COMPONENT_CPP_DEVICE_GROUP_H_
#define LIB_DRIVER_COMPONENT_CPP_DEVICE_GROUP_H_

#include <fidl/fuchsia.driver.framework/cpp/fidl.h>

#include <string_view>

namespace driver {

// Deprecated int keys with int values
inline fuchsia_driver_framework::BindRule MakeBindRule(
    const uint32_t key, const fuchsia_driver_framework::Condition condition,
    cpp20::span<const uint32_t> values) {
  std::vector<fuchsia_driver_framework::NodePropertyValue> values_vec;
  values_vec.reserve(values.size());
  for (auto val : values) {
    values_vec.push_back(fuchsia_driver_framework::NodePropertyValue::WithIntValue(val));
  }

  return fuchsia_driver_framework::BindRule(
      fuchsia_driver_framework::NodePropertyKey::WithIntValue(key), condition, values_vec);
}

inline fuchsia_driver_framework::BindRule MakeBindRule(
    const uint32_t key, const fuchsia_driver_framework::Condition condition, const uint32_t value) {
  return MakeBindRule(key, condition, {{value}});
}

inline fuchsia_driver_framework::BindRule MakeAcceptBindRule(const uint32_t key,
                                                             const uint32_t value) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kAccept, value);
}

inline fuchsia_driver_framework::BindRule MakeAcceptBindRule(const uint32_t key,
                                                             cpp20::span<const uint32_t> values) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kAccept, values);
}

inline fuchsia_driver_framework::BindRule MakeRejectBindRule(const uint32_t key,
                                                             const uint32_t value) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kReject, value);
}

inline fuchsia_driver_framework::BindRule MakeRejectBindRule(const uint32_t key,
                                                             cpp20::span<const uint32_t> values) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kReject, values);
}

// String keys with string values
inline fuchsia_driver_framework::BindRule MakeBindRule(
    const std::string_view key, const fuchsia_driver_framework::Condition condition,
    cpp20::span<const std::string_view> values) {
  std::vector<fuchsia_driver_framework::NodePropertyValue> values_vec;
  values_vec.reserve(values.size());
  for (auto val : values) {
    values_vec.push_back(
        fuchsia_driver_framework::NodePropertyValue::WithStringValue(std::string(val)));
  }

  return fuchsia_driver_framework::BindRule(
      fuchsia_driver_framework::NodePropertyKey::WithStringValue(std::string(key)), condition,
      values_vec);
}

inline fuchsia_driver_framework::BindRule MakeBindRule(
    const std::string_view key, const fuchsia_driver_framework::Condition condition,
    const std::string_view value) {
  return MakeBindRule(key, condition, cpp20::span<const std::string_view>{{value}});
}

inline fuchsia_driver_framework::BindRule MakeAcceptBindRule(const std::string_view key,
                                                             const std::string_view value) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kAccept, value);
}

inline fuchsia_driver_framework::BindRule MakeAcceptBindRule(
    const std::string_view key, cpp20::span<const std::string_view> values) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kAccept, values);
}

inline fuchsia_driver_framework::BindRule MakeRejectBindRule(const std::string_view key,
                                                             const std::string_view value) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kReject, value);
}

inline fuchsia_driver_framework::BindRule MakeRejectBindRule(
    const std::string_view key, cpp20::span<const std::string_view> values) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kReject, values);
}

// String keys with char* values
inline fuchsia_driver_framework::BindRule MakeBindRule(
    const std::string_view key, const fuchsia_driver_framework::Condition condition,
    cpp20::span<const char*> values) {
  std::vector<std::string_view> vec;
  vec.reserve(values.size());
  for (auto val : values) {
    vec.push_back(val);
  }
  return MakeBindRule(key, condition, vec);
}

inline fuchsia_driver_framework::BindRule MakeBindRule(
    const std::string_view key, const fuchsia_driver_framework::Condition condition,
    const char* value) {
  return MakeBindRule(key, condition, std::string_view(value));
}

inline fuchsia_driver_framework::BindRule MakeAcceptBindRule(const std::string_view key,
                                                             const char* value) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kAccept, value);
}

inline fuchsia_driver_framework::BindRule MakeAcceptBindRule(const std::string_view key,
                                                             cpp20::span<const char*> values) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kAccept, values);
}

inline fuchsia_driver_framework::BindRule MakeRejectBindRule(const std::string_view key,
                                                             const char* value) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kReject, value);
}

inline fuchsia_driver_framework::BindRule MakeRejectBindRule(const std::string_view key,
                                                             cpp20::span<const char*> values) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kReject, values);
}

// String keys with bool values
inline fuchsia_driver_framework::BindRule MakeBindRule(
    const std::string_view key, const fuchsia_driver_framework::Condition condition,
    cpp20::span<const bool> values) {
  std::vector<fuchsia_driver_framework::NodePropertyValue> values_vec;
  values_vec.reserve(values.size());
  for (auto val : values) {
    values_vec.push_back(fuchsia_driver_framework::NodePropertyValue::WithBoolValue(val));
  }

  return fuchsia_driver_framework::BindRule(
      fuchsia_driver_framework::NodePropertyKey::WithStringValue(std::string(key)), condition,
      values_vec);
}

inline fuchsia_driver_framework::BindRule MakeBindRule(
    const std::string_view key, const fuchsia_driver_framework::Condition condition,
    const bool value) {
  return MakeBindRule(key, condition, {{value}});
}

inline fuchsia_driver_framework::BindRule MakeAcceptBindRule(const std::string_view key,
                                                             const bool value) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kAccept, value);
}

inline fuchsia_driver_framework::BindRule MakeAcceptBindRule(const std::string_view key,
                                                             cpp20::span<const bool> values) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kAccept, values);
}

inline fuchsia_driver_framework::BindRule MakeRejectBindRule(const std::string_view key,
                                                             const bool value) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kReject, value);
}

inline fuchsia_driver_framework::BindRule MakeRejectBindRule(const std::string_view key,
                                                             cpp20::span<const bool> values) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kReject, values);
}

// String keys with int values
inline fuchsia_driver_framework::BindRule MakeBindRule(
    const std::string_view key, const fuchsia_driver_framework::Condition condition,
    cpp20::span<const uint32_t> values) {
  std::vector<fuchsia_driver_framework::NodePropertyValue> values_vec;
  values_vec.reserve(values.size());
  for (auto val : values) {
    values_vec.push_back(fuchsia_driver_framework::NodePropertyValue::WithIntValue(val));
  }

  return fuchsia_driver_framework::BindRule(
      fuchsia_driver_framework::NodePropertyKey::WithStringValue(std::string(key)), condition,
      values_vec);
}

inline fuchsia_driver_framework::BindRule MakeBindRule(
    const std::string_view key, const fuchsia_driver_framework::Condition condition,
    const uint32_t value) {
  return MakeBindRule(key, condition, cpp20::span<const uint32_t>{{value}});
}

inline fuchsia_driver_framework::BindRule MakeAcceptBindRule(const std::string_view key,
                                                             const uint32_t value) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kAccept, value);
}

inline fuchsia_driver_framework::BindRule MakeAcceptBindRule(const std::string_view key,
                                                             cpp20::span<const uint32_t> values) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kAccept, values);
}

inline fuchsia_driver_framework::BindRule MakeRejectBindRule(const std::string_view key,
                                                             const uint32_t value) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kReject, value);
}

inline fuchsia_driver_framework::BindRule MakeRejectBindRule(const std::string_view key,
                                                             cpp20::span<const uint32_t> values) {
  return MakeBindRule(key, fuchsia_driver_framework::Condition::kReject, values);
}

// String keys with enum values
inline fuchsia_driver_framework::BindRule MakeEnumBindRule(
    const std::string_view key, const fuchsia_driver_framework::Condition condition,
    cpp20::span<const std::string_view> values) {
  std::vector<fuchsia_driver_framework::NodePropertyValue> values_vec;
  values_vec.reserve(values.size());
  for (auto val : values) {
    values_vec.push_back(
        fuchsia_driver_framework::NodePropertyValue::WithEnumValue(std::string(val)));
  }

  return fuchsia_driver_framework::BindRule(
      fuchsia_driver_framework::NodePropertyKey::WithStringValue(std::string(key)), condition,
      values_vec);
}

inline fuchsia_driver_framework::BindRule MakeEnumBindRule(
    const std::string_view key, const fuchsia_driver_framework::Condition condition,
    const std::string_view value) {
  return MakeEnumBindRule(key, condition, cpp20::span<const std::string_view>{{value}});
}

inline fuchsia_driver_framework::BindRule MakeAcceptEnumBindRule(const std::string_view key,
                                                                 const std::string_view value) {
  return MakeEnumBindRule(key, fuchsia_driver_framework::Condition::kAccept, value);
}

inline fuchsia_driver_framework::BindRule MakeAcceptEnumBindRule(
    const std::string_view key, cpp20::span<const std::string_view> values) {
  return MakeEnumBindRule(key, fuchsia_driver_framework::Condition::kAccept, values);
}

inline fuchsia_driver_framework::BindRule MakeRejectEnumBindRule(const std::string_view key,
                                                                 const std::string_view value) {
  return MakeEnumBindRule(key, fuchsia_driver_framework::Condition::kReject, value);
}

inline fuchsia_driver_framework::BindRule MakeRejectEnumBindRule(
    const std::string_view key, cpp20::span<const std::string_view> values) {
  return MakeEnumBindRule(key, fuchsia_driver_framework::Condition::kReject, values);
}

}  // namespace driver

#endif  // LIB_DRIVER_COMPONENT_CPP_DEVICE_GROUP_H_
