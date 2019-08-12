// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_OT_RADIO_OT_RADIO_H_
#define ZIRCON_SYSTEM_DEV_OT_RADIO_OT_RADIO_H_

#ifndef _ALL_SOURCE
#define _ALL_SOURCE  // Enables thrd_create_with_name in <threads.h>.
#endif
#include <threads.h>

#include <lib/zx/interrupt.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <atomic>

#include <ddk/device.h>
#include <ddk/protocol/gpio.h>
#include <ddktl/device.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>

enum {
  OT_RADIO_INT_PIN,
  OT_RADIO_RESET_PIN,
  OT_RADIO_PIN_COUNT,
};

namespace ot_radio {
class OtRadioDevice : public ddk::Device<OtRadioDevice, ddk::Unbindable> {
 public:
  explicit OtRadioDevice(zx_device_t* device);

  static zx_status_t Create(void* ctx, zx_device_t* device);

  void DdkRelease();
  void DdkUnbind();

 private:
  zx_status_t Init();
  zx_status_t ShutDown();

  int Thread();
  void Reset();

  gpio_protocol_t gpio_[OT_RADIO_PIN_COUNT];
  zx::port port_;
  zx::interrupt interrupt_;

  thrd_t thread_;
};
}  // namespace ot_radio

#endif  // ZIRCON_SYSTEM_DEV_OT_RADIO_OT_RADIO_H_
