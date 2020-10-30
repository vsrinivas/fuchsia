// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers//ftl/metrics.h"

#include <lib/inspect/cpp/reader.h>

#include <zxtest/zxtest.h>

namespace ftl {
namespace {
TEST(MetricsTest, GetInspectVmoReflectsExistingMetrics) {
  Metrics metrics;
  auto hierarchy = inspect::ReadFromVmo(metrics.DuplicateInspectVmo()).take_value();
  for (const auto& property_name : ftl::Metrics::GetPropertyNames<inspect::UintProperty>()) {
    auto* property = hierarchy.node().get_property<inspect::UintPropertyValue>(property_name);
    EXPECT_NOT_NULL(property, "Missing Inspect Property: %s", property_name.c_str());
  }

  for (const auto& property_name : ftl::Metrics::GetPropertyNames<inspect::DoubleProperty>()) {
    auto* property = hierarchy.node().get_property<inspect::DoublePropertyValue>(property_name);
    EXPECT_NOT_NULL(property, "Missing Inspect Property: %s", property_name.c_str());
  }
}

TEST(MetricsTest, MetricsInitializedToZero) {
  Metrics metrics;
  auto hierarchy = inspect::ReadFromVmo(metrics.DuplicateInspectVmo()).take_value();
  for (const auto& property_name : ftl::Metrics::GetPropertyNames<inspect::UintProperty>()) {
    auto* property = hierarchy.node().get_property<inspect::UintPropertyValue>(property_name);
    EXPECT_NOT_NULL(property, "Missing Inspect Property: %s", property_name.c_str());
    if (property == nullptr) {
      continue;
    }
    EXPECT_EQ(property->value(), 0);
  }

  for (const auto& property_name : ftl::Metrics::GetPropertyNames<inspect::DoubleProperty>()) {
    auto* property = hierarchy.node().get_property<inspect::DoublePropertyValue>(property_name);
    EXPECT_NOT_NULL(property, "Missing Inspect Property: %s", property_name.c_str());
    if (property == nullptr) {
      continue;
    }
    EXPECT_EQ(property->value(), 0.0);
  }
}

TEST(MetricsTest, MetricsMappedCorrectly) {
  Metrics metrics;

  std::map<std::string, uint64_t> expected_uint_values;
  std::map<std::string, double> expected_double_values;

  metrics.max_wear().Set(1);
  expected_uint_values["nand.erase_block.max_wear"] = 1;

  metrics.read().count.Set(2);
  expected_uint_values["block.read.count"] = 2;

  metrics.read().all.count.Set(3);
  expected_uint_values["block.read.issued_nand_operation.count"] = 3;

  metrics.read().all.rate.Add(4);
  expected_double_values["block.read.issued_nand_operation.average_rate"] = 4;

  metrics.read().page_read.count.Set(5);
  expected_uint_values["block.read.issued_page_read.count"] = 5;

  metrics.read().page_read.rate.Add(6);
  expected_double_values["block.read.issued_page_read.average_rate"] = 6;

  metrics.read().page_write.count.Set(7);
  expected_uint_values["block.read.issued_page_write.count"] = 7;

  metrics.read().page_write.rate.Add(8);
  expected_double_values["block.read.issued_page_write.average_rate"] = 8;

  metrics.read().block_erase.count.Set(9);
  expected_uint_values["block.read.issued_block_erase.count"] = 9;

  metrics.read().block_erase.rate.Add(10);
  expected_double_values["block.read.issued_block_erase.average_rate"] = 10;

  metrics.write().count.Set(11);
  expected_uint_values["block.write.count"] = 11;

  metrics.write().all.count.Set(12);
  expected_uint_values["block.write.issued_nand_operation.count"] = 12;

  metrics.write().all.rate.Add(13);
  expected_double_values["block.write.issued_nand_operation.average_rate"] = 13;

  metrics.write().page_read.count.Set(14);
  expected_uint_values["block.write.issued_page_read.count"] = 14;

  metrics.write().page_read.rate.Add(15);
  expected_double_values["block.write.issued_page_read.average_rate"] = 15;

  metrics.write().page_write.count.Set(16);
  expected_uint_values["block.write.issued_page_write.count"] = 16;

  metrics.write().page_write.rate.Add(17);
  expected_double_values["block.write.issued_page_write.average_rate"] = 17;

  metrics.write().block_erase.count.Set(18);
  expected_uint_values["block.write.issued_block_erase.count"] = 18;

  metrics.write().block_erase.rate.Add(19);
  expected_double_values["block.write.issued_block_erase.average_rate"] = 19;

  metrics.trim().count.Set(20);
  expected_uint_values["block.trim.count"] = 20;

  metrics.trim().all.count.Set(21);
  expected_uint_values["block.trim.issued_nand_operation.count"] = 21;

  metrics.trim().all.rate.Add(22);
  expected_double_values["block.trim.issued_nand_operation.average_rate"] = 22;

  metrics.trim().page_read.count.Set(23);
  expected_uint_values["block.trim.issued_page_read.count"] = 23;

  metrics.trim().page_read.rate.Add(24);
  expected_double_values["block.trim.issued_page_read.average_rate"] = 24;

  metrics.trim().page_write.count.Set(25);
  expected_uint_values["block.trim.issued_page_write.count"] = 25;

  metrics.trim().page_write.rate.Add(26);
  expected_double_values["block.trim.issued_page_write.average_rate"] = 26;

  metrics.trim().block_erase.count.Set(27);
  expected_uint_values["block.trim.issued_block_erase.count"] = 27;

  metrics.trim().block_erase.rate.Add(28);
  expected_double_values["block.trim.issued_block_erase.average_rate"] = 28;

  metrics.flush().count.Set(27);
  expected_uint_values["block.flush.count"] = 27;

  metrics.flush().all.count.Set(28);
  expected_uint_values["block.flush.issued_nand_operation.count"] = 28;

  metrics.flush().all.rate.Add(29);
  expected_double_values["block.flush.issued_nand_operation.average_rate"] = 29;

  metrics.flush().page_read.count.Set(30);
  expected_uint_values["block.flush.issued_page_read.count"] = 30;

  metrics.flush().page_read.rate.Add(31);
  expected_double_values["block.flush.issued_page_read.average_rate"] = 31;

  metrics.flush().page_write.count.Set(32);
  expected_uint_values["block.flush.issued_page_write.count"] = 32;

  metrics.flush().page_write.rate.Add(33);
  expected_double_values["block.flush.issued_page_write.average_rate"] = 33;

  metrics.flush().block_erase.count.Set(34);
  expected_uint_values["block.flush.issued_block_erase.count"] = 34;

  metrics.flush().block_erase.rate.Add(35);
  expected_double_values["block.flush.issued_block_erase.average_rate"] = 35;

  auto hierarchy = inspect::ReadFromVmo(metrics.DuplicateInspectVmo()).take_value();
  for (const auto& property_name : ftl::Metrics::GetPropertyNames<inspect::UintProperty>()) {
    auto* property = hierarchy.node().get_property<inspect::UintPropertyValue>(property_name);
    EXPECT_NOT_NULL(property, "Missing Inspect Property: %s", property_name.c_str());
    EXPECT_EQ(property->value(), expected_uint_values[property_name], "Property value mismatch %s",
              property_name.c_str());
  }
  for (const auto& property_name : ftl::Metrics::GetPropertyNames<inspect::DoubleProperty>()) {
    auto* property = hierarchy.node().get_property<inspect::DoublePropertyValue>(property_name);
    EXPECT_NOT_NULL(property, "Missing Inspect Property: %s", property_name.c_str());
    EXPECT_EQ(property->value(), expected_double_values[property_name],
              "Property value mismatch %s", property_name.c_str());
  }
}

}  // namespace
}  // namespace ftl
