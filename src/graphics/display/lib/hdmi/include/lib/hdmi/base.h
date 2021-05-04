// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_LIB_HDMI_INCLUDE_LIB_HDMI_BASE_H_
#define SRC_GRAPHICS_DISPLAY_LIB_HDMI_INCLUDE_LIB_HDMI_BASE_H_

#include <stdint.h>

class HdmiIpBase {
 public:
  HdmiIpBase() = default;
  virtual ~HdmiIpBase() = default;

  virtual void WriteIpReg(uint32_t addr, uint32_t data) = 0;
  virtual uint32_t ReadIpReg(uint32_t addr) = 0;
};

#endif  // SRC_GRAPHICS_DISPLAY_LIB_HDMI_INCLUDE_LIB_HDMI_BASE_H_
