// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OPENTHREAD_DRIVERS_OT_RADIO_OT_RADIO_H_
#define SRC_CONNECTIVITY_OPENTHREAD_DRIVERS_OT_RADIO_OT_RADIO_H_

#ifndef _ALL_SOURCE
#define _ALL_SOURCE  // Enables thrd_create_with_name in <threads.h>.
#endif
#include <lib/async-loop/cpp/loop.h>
#include <lib/sync/completion.h>
#include <lib/zx/interrupt.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <array>
#include <atomic>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/spi.h>
#include <fbl/mutex.h>

#include "spinel_framer.h"

enum {
  OT_RADIO_INT_PIN,
  OT_RADIO_RESET_PIN,
  OT_RADIO_PIN_COUNT,
};

namespace ot {
class OtRadioDevice : public ddk::Device<OtRadioDevice, ddk::UnbindableNew> {
 public:
  explicit OtRadioDevice(zx_device_t* device);

  static zx_status_t Create(void* ctx, zx_device_t* parent, std::unique_ptr<OtRadioDevice>* out);
  static zx_status_t CreateBindAndStart(void* ctx, zx_device_t* parent);
  zx_status_t Bind(void);
  zx_status_t Start(void);
  static bool RunUnitTests(void* ctx, zx_device_t* parent, zx_handle_t channel);
  zx_status_t Init();

  void DdkRelease();
  void DdkUnbindNew(ddk::UnbindTxn txn);
  zx_status_t ShutDown();
  void RemoveDevice();
  void FreeDevice();
  zx_status_t Reset();
  zx_status_t GetNCPVersion();

  zx::port port_;
  zx::interrupt interrupt_;
  ddk::SpiProtocolClient spi_;
  sync_completion_t spi_rx_complete_;

  uint8_t spi_rx_buffer_[2048];

 private:
  zx_status_t RadioThread();
  zx_status_t StartLoopThread();
  zx_status_t ReadRadioPacket();
  zx_status_t HandleRadioRxFrame(uint8_t* frameBuffer, uint16_t length);
  zx_status_t RadioPacketTx(uint8_t* frameBuffer, uint16_t length);

  std::array<ddk::GpioProtocolClient, OT_RADIO_PIN_COUNT> gpio_;
  thrd_t thread_;
  async::Loop loop_;
  std::unique_ptr<ot::SpinelFramer> spinel_framer_;
  uint16_t spi_rx_buffer_len_ = 0;
  uint16_t spi_tx_buffer_len_ = 0;
  uint8_t spi_tx_buffer_[2048];
};
}  // namespace ot

#endif  // SRC_CONNECTIVITY_OPENTHREAD_DRIVERS_OT_RADIO_OT_RADIO_H_
