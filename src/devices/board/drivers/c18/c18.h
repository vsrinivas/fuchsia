// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_C18_C18_H_
#define SRC_DEVICES_BOARD_DRIVERS_C18_C18_H_

#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/protocol/gpioimpl.h>
#include <ddktl/protocol/platform/bus.h>

namespace board_c18 {

// BTI IDs for our devices
enum {
  BTI_MSDC0,
};

// These should match the mmio table defined in c18-spi.c
enum { C18_SPI0, C18_SPI1, C18_SPI2, C18_SPI3, C18_SPI4, C18_SPI5 };

class C18;
using C18Type = ddk::Device<C18>;

class C18 : public C18Type {
 public:
  explicit C18(zx_device_t* parent, pbus_protocol_t* pbus) : C18Type(parent), pbus_(pbus) {}
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkRelease();

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(C18);

  zx_status_t Start();

  static zx_status_t SocInit();
  zx_status_t GpioInit();
  zx_status_t Msdc0Init();
  zx_status_t SpiInit();

  int Thread();

  ddk::PBusProtocolClient pbus_;
  ddk::GpioImplProtocolClient gpio_impl_;
  thrd_t thread_;
};

}  // namespace board_c18

#endif  // SRC_DEVICES_BOARD_DRIVERS_C18_C18_H_
