// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_BOARD_C18_C18_H_
#define ZIRCON_SYSTEM_DEV_BOARD_C18_C18_H_

#include <threads.h>

#include <ddk/protocol/gpioimpl.h>
#include <ddktl/device.h>
#include <ddktl/protocol/platform/bus.h>

namespace board_c18 {

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

  int Thread();

  ddk::PBusProtocolClient pbus_;
  gpio_impl_protocol_t gpio_impl_;
  thrd_t thread_;
};

}  // namespace board_c18

#endif  // ZIRCON_SYSTEM_DEV_BOARD_C18_C18_H_
