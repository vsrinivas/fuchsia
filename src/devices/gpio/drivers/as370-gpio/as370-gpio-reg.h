// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_GPIO_DRIVERS_AS370_GPIO_AS370_GPIO_REG_H_
#define SRC_DEVICES_GPIO_DRIVERS_AS370_GPIO_AS370_GPIO_REG_H_

#include <hwreg/bitfields.h>

namespace gpio {

class IoCntl : public hwreg::RegisterBase<IoCntl, uint32_t> {
 public:
  static auto Get(uint32_t index) { return hwreg::RegisterAddr<IoCntl>(0x60 + (4 * index)); }

  DEF_BIT(3, puen);
  DEF_BIT(2, pden);
  DEF_FIELD(1, 0, drv);
};

}  // namespace gpio

#endif  // SRC_DEVICES_GPIO_DRIVERS_AS370_GPIO_AS370_GPIO_REG_H_
