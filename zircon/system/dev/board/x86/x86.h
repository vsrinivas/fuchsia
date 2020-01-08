// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_BOARD_X86_X86_H_
#define ZIRCON_SYSTEM_DEV_BOARD_X86_X86_H_

#include <threads.h>

#include <memory>

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
  ~X86();

  static zx_status_t Create(void* ctx, zx_device_t* parent, std::unique_ptr<X86>* out);
  static zx_status_t CreateAndBind(void* ctx, zx_device_t* parent);
  static bool RunUnitTests(void* ctx, zx_device_t* parent, zx_handle_t channel);

  // Device protocol implementation.
  void DdkRelease();

  // Performs ACPICA initialization.
  zx_status_t EarlyAcpiInit();

  zx_status_t EarlyInit();

 private:
  X86(const X86&) = delete;
  X86(X86&&) = delete;
  X86& operator=(const X86&) = delete;
  X86& operator=(X86&&) = delete;

  zx_status_t SysmemInit();

  // Register this instance with devmgr and launch the deferred initialization in Thread.
  zx_status_t Bind();
  zx_status_t Start();
  int Thread();

  ddk::PBusProtocolClient pbus_;

  // This is our parents parent.
  zx_device_t* sys_root_;

  thrd_t thread_;

  // Whether the global ACPICA initialization has been performed or not
  bool acpica_initialized_ = false;
};

}  // namespace x86

#endif  // ZIRCON_SYSTEM_DEV_BOARD_X86_X86_H_
