// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_I2C_DRIVERS_INTEL_I2C_INTEL_I2C_SUBORDINATE_H_
#define SRC_DEVICES_I2C_DRIVERS_INTEL_I2C_INTEL_I2C_SUBORDINATE_H_

#include <stdint.h>
#include <zircon/types.h>

#include <memory>

namespace intel_i2c {

inline constexpr uint8_t kI2c7BitAddress = 7;
inline constexpr uint8_t kI2c10BitAddress = 10;

class IntelI2cController;

struct IntelI2cSubordinateSegment {
  int type;
  int len;
  uint8_t* buf;
};

class IntelI2cSubordinate {
 public:
  static std::unique_ptr<IntelI2cSubordinate> Create(IntelI2cController* controller,
                                                     const uint8_t chip_address_width,
                                                     const uint16_t chip_address,
                                                     const uint32_t i2c_class);

  zx_status_t Transfer(const IntelI2cSubordinateSegment* segments, int segment_count);
  uint8_t GetChipAddressWidth() const { return chip_address_width_; }
  uint16_t GetChipAddress() const { return chip_address_; }
  uint32_t GetI2cClass() const { return i2c_class_; }

 private:
  IntelI2cSubordinate(IntelI2cController* controller, const uint8_t chip_address_width,
                      const uint16_t chip_address, const uint32_t i2c_class)
      : controller_(controller),
        chip_address_width_(chip_address_width),
        chip_address_(chip_address),
        i2c_class_(i2c_class) {}
  IntelI2cController* controller_;
  const uint8_t chip_address_width_;
  const uint16_t chip_address_;
  const uint32_t i2c_class_;
};

}  // namespace intel_i2c

#endif  // SRC_DEVICES_I2C_DRIVERS_INTEL_I2C_INTEL_I2C_SUBORDINATE_H_
