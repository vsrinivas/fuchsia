// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_BOARD_X86_X86_H_
#define ZIRCON_SYSTEM_DEV_BOARD_X86_X86_H_

#include <threads.h>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/platform/bus.h>
#include <fbl/macros.h>

namespace x86 {

// This is the main class for the X86 platform bus driver.
class X86 : public ddk::Device<X86> {
 public:
  explicit X86(zx_device_t* parent, pbus_protocol_t* pbus, zx_device_t* sys_root)
      : ddk::Device<X86>(parent), pbus_(pbus), sys_root_(sys_root) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkRelease();

 private:
  X86(const X86&) = delete;
  X86(X86&&) = delete;
  X86& operator=(const X86&) = delete;
  X86& operator=(X86&&) = delete;

  zx_status_t SysmemInit();

  zx_status_t Start();
  int Thread();

  ddk::PBusProtocolClient pbus_;

  // This is our parents parent.
  zx_device_t* sys_root_;

  thrd_t thread_;
};

}  // namespace x86

#endif  // ZIRCON_SYSTEM_DEV_BOARD_X86_X86_H_
