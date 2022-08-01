// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MAILBOX_DRIVERS_AML_MAILBOX_AML_MAILBOX_H_
#define SRC_DEVICES_MAILBOX_DRIVERS_AML_MAILBOX_AML_MAILBOX_H_

#include <fuchsia/hardware/mailbox/cpp/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/interrupt.h>

#include <ddktl/device.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>

#include "meson_mhu_common.h"

#define AOCPU_MAILBOX_TEST 0
#define DSP_MAILBOX_TEST 0

struct mbox_uint {
  uint32_t uintcmd;
  uint8_t data[RPCUINT_SIZE];
  uint32_t sumdata;
};

namespace aml_mailbox {

class AmlMailbox;

using DeviceType = ddk::Device<AmlMailbox, ddk::Unbindable>;

class AmlMailbox : public DeviceType, public ddk::MailboxProtocol<AmlMailbox, ddk::base_protocol> {
 public:
  explicit AmlMailbox(zx_device_t* parent, ddk::MmioBuffer mbox_wrmmio, ddk::MmioBuffer mbox_rdmmio,
                      ddk::MmioBuffer mbox_fsetmmio, ddk::MmioBuffer mbox_fclrmmio,
                      ddk::MmioBuffer mbox_fstsmmio, ddk::MmioBuffer mbox_irqmmio,
                      zx::interrupt& irq)
      : DeviceType(parent),
        mbox_wrmmio_(std::move(mbox_wrmmio)),
        mbox_rdmmio_(std::move(mbox_rdmmio)),
        mbox_fsetmmio_(std::move(mbox_fsetmmio)),
        mbox_fclrmmio_(std::move(mbox_fclrmmio)),
        mbox_fstsmmio_(std::move(mbox_fstsmmio)),
        mbox_irqmmio_(std::move(mbox_irqmmio)),
        irq_(std::move(irq)) {}

  ~AmlMailbox() = default;
  zx_status_t Init();
  void ShutDown();
  int IrqThread();
  static zx_status_t Create(void* ctx, zx_device_t* parent);
  uint64_t MboxIrqStatus();
  void MboxAckIsrHandler(uint32_t mhu_id);
  void MboxIsrHandler(uint32_t mhu_id);
  void MboxFifoWrite(uint32_t offset, const uint8_t* from, long count);
  void MboxFifoRead(uint32_t offset, uint8_t* to, long count);
  void MboxFifoClr(uint32_t offset, long count);
  void MboxIrqClean(uint64_t mask);
  int ToSendIdx(int idx, bool send);

  // Methods required by the ddk mixins.
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  zx_status_t MailboxSendCommand(const mailbox_channel_t* channel, const mailbox_data_buf_t* mdata);
  void AocpuMboxMessageRead(mailbox_channel_t* channel, const mailbox_data_buf_t* mdata);
  void DspMboxMessageRead(mailbox_channel_t* channel, const mailbox_data_buf_t* mdata);
  zx_status_t DspMboxMessageWrite(const mailbox_channel_t* channel,
                                  const mailbox_data_buf_t* mdata);
  zx_status_t AocpuMboxMessageWrite(const mailbox_channel_t* channel,
                                    const mailbox_data_buf_t* mdata);
  void MailboxSendData(int idx, uint32_t cmd, uint8_t* data, int tx_size);

 private:
  ddk::MmioBuffer mbox_wrmmio_;
  ddk::MmioBuffer mbox_rdmmio_;
  ddk::MmioBuffer mbox_fsetmmio_;
  ddk::MmioBuffer mbox_fclrmmio_;
  ddk::MmioBuffer mbox_fstsmmio_;
  ddk::MmioBuffer mbox_irqmmio_;
  zx::interrupt irq_;
  thrd_t irq_thread_;
  uint32_t mbox_id[MBOX_MAX];
  uint32_t rx_flag[MBOX_MAX];
  uint32_t mhu_irqctlr;
  uint32_t mhu_irqclr;
  uint32_t mhu_irqmax;
  uint32_t num_chans;
  mailbox_channel_t* channels;
#if AOCPU_MAILBOX_TEST
  int TestAocpuThread();
#endif
#if DSP_MAILBOX_TEST
  int TestDspThread();
#endif
};

}  // namespace aml_mailbox

#endif  // SRC_DEVICES_MAILBOX_DRIVERS_AML_MAILBOX_AML_MAILBOX_H_
