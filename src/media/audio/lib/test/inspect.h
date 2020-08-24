// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_INSPECT_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_INSPECT_H_

#include <unordered_map>
#include <unordered_set>

#include <gtest/gtest.h>

#include "zircon/system/ulib/inspect/include/lib/inspect/cpp/hierarchy.h"

namespace media::audio::test {

// Describes a set of properties that must exist at an inspect node.
struct ExpectedInspectProperties {
  std::unordered_map<std::string, ExpectedInspectProperties> children;
  // Properties that must have specific values.
  std::unordered_map<std::string, double> double_values;
  std::unordered_map<std::string, uint64_t> uint_values;
  // Properties that must have any non-zero value.
  std::unordered_set<std::string> double_nonzero;
  std::unordered_set<std::string> uint_nonzero;

  // Shorthands to make calling code more readable.
  void ExpectDoubleNonzero(const std::string& property_name) {
    double_nonzero.insert(property_name);
  }
  void ExpectUintNonzero(const std::string& property_name) { uint_nonzero.insert(property_name); }

  // Compare the properties at the given hierachy to the expected values.
  void Check(const std::string& path, const inspect::Hierarchy& h) const {
    for (auto& [name, expected_child] : children) {
      auto child = h.GetByPath({name});
      if (!child) {
        ADD_FAILURE() << "missing node: " << path << "/" << name;
        continue;
      }
      expected_child.Check(path + "/" + name, *child);
    }
    CheckValue<inspect::DoublePropertyValue>(path, h.node(), double_values);
    CheckValue<inspect::UintPropertyValue>(path, h.node(), uint_values);
    CheckNonZero<inspect::DoublePropertyValue, double>(path, h.node(), double_nonzero);
    CheckNonZero<inspect::UintPropertyValue, uint64_t>(path, h.node(), uint_nonzero);
  }

 private:
  template <typename PropertyT, typename T>
  static void CheckValue(const std::string& path, const inspect::NodeValue& node,
                         const std::unordered_map<std::string, T>& expected_values) {
    for (auto& [name, expected_value] : expected_values) {
      auto p = node.get_property<PropertyT>(name);
      if (!p) {
        ADD_FAILURE() << "missing property: " << path << "[" << name << "]";
        continue;
      }
      EXPECT_EQ(expected_value, p->value()) << "at property " << path << "[" << name << "]";
    }
  }

  template <typename PropertyT, typename T>
  static void CheckNonZero(const std::string& path, const inspect::NodeValue& node,
                           const std::unordered_set<std::string>& properties) {
    for (auto& name : properties) {
      auto p = node.get_property<PropertyT>(name);
      if (!p) {
        ADD_FAILURE() << "missing property: " << path << "[" << name << "]";
        continue;
      }
      EXPECT_NE(static_cast<T>(0), p->value()) << "at property " << path << "[" << name << "]";
    }
  }
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_INSPECT_H_
