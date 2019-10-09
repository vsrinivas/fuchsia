// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <lib/device-protocol/platform-device.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <lib/mmio/mmio.h>
#include <lib/device-protocol/pdev.h>
#include <ddktl/protocol/mailbox.h>
#include <hw/reg.h>
#include <lib/sync/completion.h>
#include <lib/zx/interrupt.h>
#include <threads.h>

#include <optional>

#define GET_NUM_WORDS(x) ((x) / 4 + (((x) % 4) ? 1 : 0))

namespace mailbox {

class AmlMailbox;
using DeviceType = ddk::Device<AmlMailbox, ddk::UnbindableNew>;

class AmlMailbox : public DeviceType, public ddk::MailboxProtocol<AmlMailbox, ddk::base_protocol> {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlMailbox);

  explicit AmlMailbox(zx_device_t* parent) : DeviceType(parent), pdev_(parent) {}

  static zx_status_t Create(zx_device_t* parent);

  // DDK Hooks.
  void DdkRelease();
  void DdkUnbindNew(ddk::UnbindTxn txn);

  // ZX_PROTOCOL_MAILBOX protocol.
  zx_status_t MailboxSendCommand(const mailbox_channel_t* channel, const mailbox_data_buf_t* mdata);

 private:
  static constexpr uint32_t kNumMailboxes = 6;

  // MMIO Indexes
  enum {
    MMIO_MAILBOX,
    MMIO_MAILBOX_PAYLOAD,
  };

  // IRQ Indexes
  enum {
    MAILBOX_IRQ_RECEIV0,
    MAILBOX_IRQ_RECEIV1,
    MAILBOX_IRQ_RECEIV2,
    MAILBOX_IRQ_SEND3,
    MAILBOX_IRQ_SEND4,
    MAILBOX_IRQ_SEND5,
  };

  zx_status_t InitPdev();
  zx_status_t Bind();
  mailbox_type_t GetRxMailbox(mailbox_type_t tx_mailbox);
  size_t GetNumWords(size_t size);

  ddk::PDev pdev_;
  zx::interrupt inth_[kNumMailboxes];
  mtx_t mailbox_chan_lock_[kNumMailboxes];

  std::optional<ddk::MmioBuffer> mailbox_mmio_;
  std::optional<ddk::MmioBuffer> mailbox_payload_mmio_;
};

}  // namespace mailbox
