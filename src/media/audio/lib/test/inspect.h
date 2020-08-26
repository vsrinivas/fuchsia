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
  std::unordered_map<std::string, double> doubles;
  std::unordered_map<std::string, uint64_t> uints;

  // Properties that must have any non-zero value.
  std::unordered_set<std::string> nonzero_doubles;
  std::unordered_set<std::string> nonzero_uints;

  // Compare the properties at the given hierachy to the expected values.
  // The path is used for debug output.
  static void Check(const ExpectedInspectProperties& props, const std::string& path,
                    const inspect::Hierarchy& h) {
    for (auto& [name, expected_child] : props.children) {
      auto child = h.GetByPath({name});
      if (!child) {
        ADD_FAILURE() << "missing node: " << path << "/" << name;
        continue;
      }
      Check(expected_child, path + "/" + name, *child);
    }
    CheckValue<inspect::DoublePropertyValue>(path, h.node(), props.doubles);
    CheckValue<inspect::UintPropertyValue>(path, h.node(), props.uints);
    CheckNonZero<inspect::DoublePropertyValue, double>(path, h.node(), props.nonzero_doubles);
    CheckNonZero<inspect::UintPropertyValue, uint64_t>(path, h.node(), props.nonzero_uints);
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
