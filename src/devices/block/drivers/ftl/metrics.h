// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_FTL_METRICS_H_
#define SRC_DEVICES_BLOCK_DRIVERS_FTL_METRICS_H_

#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/vmo/types.h>

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <fbl/span.h>

namespace ftl {

// Helper property wrapper that caches the value, in order to update
// the inspect property with the correct value, since properties are write only.
class RateProperty {
 public:
  explicit RateProperty(inspect::DoubleProperty property) : property_(std::move(property)) {}

  // Returns the accumulated rate as a weighted rate.
  double rate() const { return rate_.GetValue(); }

  // Updates the accumulated rate.
  void Add(int extra_accumulated) {
    rate_.accumulated += extra_accumulated;
    rate_.entries++;
    property_.Set(rate());
  }

 private:
  // Helper for keeping track and updating an accumulated rate.
  struct Rate {
    constexpr double GetValue() const {
      if (entries == 0) {
        return 0;
      }
      return static_cast<double>(accumulated) / entries;
    }

    int accumulated = 0;
    int entries = 0;
  };

  Rate rate_;
  inspect::DoubleProperty property_;
};

struct NestedNandOperationProperties {
  NestedNandOperationProperties(inspect::UintProperty count, inspect::DoubleProperty rate)
      : count(std::move(count)), rate(std::move(rate)) {}

  // Number of nand operations issued for a given type.
  inspect::UintProperty count;

  // Rate at which operations of this type are issued to the underlying device.
  RateProperty rate;
};

// For each type of block operation we keep the number of operations issued and the accumulated rate
// at which an operation is issued as a result of an incoming block operation into the FTL.
struct BlockOperationProperties {
  // Number of block operations of a given type that have been processed by the FTL.
  inspect::UintProperty count;

  // Operation stats per nand operation type for operation issued for this block operation type.
  NestedNandOperationProperties all;
  NestedNandOperationProperties page_read;
  NestedNandOperationProperties page_write;
  NestedNandOperationProperties block_erase;
};

// Encapsulates all existing metrics, and the property list names for each.
class Metrics {
 public:
  // Each of this functions returns the name of the property for a count or rate for the given pair.
  // Unknown combinations will return an empty string.
  static std::string GetMaxWearPropertyName() { return "max_wear"; }

  // Returns the list of expected properties in the hierarchy for each inspect metric type.
  template <typename InspectMetricType>
  static std::vector<std::string> GetPropertyNames();

  Metrics();

  inspect::UintProperty& max_wear() { return max_wear_; }

  BlockOperationProperties& read() { return read_; }
  BlockOperationProperties& write() { return write_; }
  BlockOperationProperties& trim() { return trim_; }
  BlockOperationProperties& flush() { return flush_; }

  zx::vmo DuplicateInspectVmo() const { return inspector_.DuplicateVmo(); }

 private:
  // Inspector root.
  inspect::Inspector inspector_;

  // Current maximum wear over all nand blocks.
  inspect::UintProperty max_wear_;

  // Properties for each block operation type.
  BlockOperationProperties read_;
  BlockOperationProperties write_;
  BlockOperationProperties flush_;
  BlockOperationProperties trim_;
};

}  // namespace ftl

#endif  // SRC_DEVICES_BLOCK_DRIVERS_FTL_METRICS_H_
