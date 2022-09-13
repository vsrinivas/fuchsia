// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MAILBOX_DRIVERS_AML_MAILBOX_AML_MAILBOX_H_
#define SRC_DEVICES_MAILBOX_DRIVERS_AML_MAILBOX_AML_MAILBOX_H_

#include <fidl/fuchsia.hardware.mailbox/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <lib/svc/outgoing.h>
#include <lib/zx/interrupt.h>

#include <ddktl/device.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>

#include "src/devices/mailbox/drivers/aml-mailbox/meson_mhu_common.h"

namespace aml_mailbox {

class AmlMailbox;
namespace FidlMailbox = fuchsia_hardware_mailbox;

using DeviceType =
    ddk::Device<AmlMailbox, ddk::Unbindable, ddk::Messageable<FidlMailbox::Device>::Mixin>;

class AmlMailbox : public DeviceType {
 public:
  AmlMailbox(zx_device_t* parent, fdf::MmioBuffer mbox_wrmmio, fdf::MmioBuffer mbox_rdmmio,
             fdf::MmioBuffer mbox_fsetmmio, fdf::MmioBuffer mbox_fclrmmio,
             fdf::MmioBuffer mbox_fstsmmio, fdf::MmioBuffer mbox_irqmmio, zx::interrupt irq,
             async_dispatcher_t* dispatcher)
      : DeviceType(parent),
        mbox_wrmmio_(std::move(mbox_wrmmio)),
        mbox_rdmmio_(std::move(mbox_rdmmio)),
        mbox_fsetmmio_(std::move(mbox_fsetmmio)),
        mbox_fclrmmio_(std::move(mbox_fclrmmio)),
        mbox_fstsmmio_(std::move(mbox_fstsmmio)),
        mbox_irqmmio_(std::move(mbox_irqmmio)),
        irq_(std::move(irq)),
        dispatcher_(dispatcher) {}

  ~AmlMailbox();
  zx_status_t Init();
  void ShutDown();
  int IrqThread();

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  uint64_t MboxIrqStatus();
  void MboxAckIsrHandler(uint32_t mhu_id);
  void MboxIsrHandler(uint32_t mhu_id);
  void MboxFifoWrite(uint32_t offset, const uint8_t* from, uint8_t count);
  void MboxFifoClr(uint32_t offset);
  void MboxIrqClean(uint64_t mask);

  // Methods required by the ddk mixins.
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  void MailboxSendData(uint8_t idx, uint32_t cmd, uint8_t* data, uint8_t tx_size);
  zx_status_t AocpuMboxMessageWrite(uint8_t channel, FidlMailbox::wire::MboxTx* mdata);
  zx_status_t DspMboxMessageWrite(uint8_t channel, FidlMailbox::wire::MboxTx* mdata);

  zx_status_t SendCommand(uint8_t channel, FidlMailbox::wire::MboxTx* mdata);

  // |fidl::WireServer<fuchsia_hardware_mailbox::Device>|
  void SendCommand(SendCommandRequestView request, SendCommandCompleter::Sync& completer) override;
  void ReceiveData(ReceiveDataRequestView request, ReceiveDataCompleter::Sync& completer) override;

 private:
  fdf::MmioBuffer mbox_wrmmio_;
  fdf::MmioBuffer mbox_rdmmio_;
  fdf::MmioBuffer mbox_fsetmmio_;
  fdf::MmioBuffer mbox_fclrmmio_;
  fdf::MmioBuffer mbox_fstsmmio_;
  fdf::MmioBuffer mbox_irqmmio_;
  zx::interrupt irq_;
  thrd_t irq_thread_;
  uint8_t mbox_id_[kMboxMax];
  uint8_t rx_flag_[kMboxMax];
  std::array<std::array<uint8_t, kMboxFifoSize>, kMboxMax> channels_;

  std::optional<svc::Outgoing> outgoing_;
  async_dispatcher_t* dispatcher_;
};

}  // namespace aml_mailbox

#endif  // SRC_DEVICES_MAILBOX_DRIVERS_AML_MAILBOX_AML_MAILBOX_H_
