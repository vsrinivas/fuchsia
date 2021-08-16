// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_FUSB302_INSPECTABLE_TYPES_H_
#define SRC_DEVICES_POWER_DRIVERS_FUSB302_INSPECTABLE_TYPES_H_

#include <lib/inspect/cpp/inspect.h>

#include "src/devices/power/drivers/fusb302/usb-pd.h"

namespace fusb302 {

using usb::pd::kMaxLen;
using usb::pd::kMaxObjects;
using PowerDataObject = usb::pd::DataPdMessage::PowerDataObject;

template <typename T>
class InspectableBool {
 public:
  InspectableBool(inspect::Node* parent, const std::string& name, T init_val)
      : value_(init_val), inspect_(parent->CreateBool(name, init_val)) {}

  T get() { return value_; }
  void set(T val) {
    value_ = val;
    inspect_.Set(val);
  }

 private:
  T value_;
  inspect::internal::Property<bool> inspect_;
};

template <typename T>
class InspectableUint {
 public:
  InspectableUint(inspect::Node* parent, const std::string& name, T init_val)
      : value_(init_val), inspect_(parent->CreateUint(name, init_val)) {}

  T get() { return value_; }
  void set(T val) {
    value_ = val;
    inspect_.Set(val);
  }

 private:
  T value_;
  inspect::internal::NumericProperty<uint64_t> inspect_;
};

class InspectablePdoArray {
 public:
  InspectablePdoArray(inspect::Node* parent, const std::string& name)
      : inspect_(parent->CreateUintArray(name, kMaxObjects)) {}

  size_t size() const { return array_.size(); }
  const PowerDataObject& get(size_t i) const {
    ZX_DEBUG_ASSERT(i < kMaxObjects);
    return array_[i];
  }
  void emplace_back(uint32_t val) {
    array_.emplace_back(val);
    inspect_.Set(array_.size() - 1, val);
  }
  void clear() {
    array_.clear();
    for (size_t i = 0; i < kMaxObjects; i++) {
      inspect_.Set(i, 0);
    }
  }

 private:
  std::vector<PowerDataObject> array_;
  inspect::UintArray inspect_;
};

}  // namespace fusb302

#endif  // SRC_DEVICES_POWER_DRIVERS_FUSB302_INSPECTABLE_TYPES_H_
