// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_INSPECT_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_INSPECT_H_

#include <unordered_map>

#include <gtest/gtest.h>

#include "zircon/system/ulib/inspect/include/lib/inspect/cpp/hierarchy.h"

namespace media::audio::test {

// Describes a set of properties that must exist at an inspect node.
struct ExpectedInspectProperties {
  std::unordered_map<std::string, ExpectedInspectProperties> children;
  std::unordered_map<std::string, double> double_values;
  std::unordered_map<std::string, uint64_t> uint_values;

  // Compare the properties at the given hierachy to the expected values.
  void Check(const std::string& path, const inspect::Hierarchy& h) const {
    for (auto& [name, expected_child] : children) {
      auto child = h.GetByPath({name});
      if (!child) {
        ADD_FAILURE() << "missing node: " << path << "[" << name << "]";
        continue;
      }
      expected_child.Check(path + "/" + name, *child);
    }
    DoCheck<inspect::DoublePropertyValue>(path, h.node(), double_values);
    DoCheck<inspect::UintPropertyValue>(path, h.node(), uint_values);
  }

 private:
  template <typename PropertyT, typename T>
  static void DoCheck(const std::string& path, const inspect::NodeValue& node,
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
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_INSPECT_H_
