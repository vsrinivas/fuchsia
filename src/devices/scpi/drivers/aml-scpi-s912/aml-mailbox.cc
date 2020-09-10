// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-mailbox.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

#include "aml-mailbox-hw.h"

namespace mailbox {

mailbox_type_t AmlMailbox::GetRxMailbox(mailbox_type_t tx_mailbox) {
  switch (tx_mailbox) {
    case MAILBOX_TYPE_AP_SECURE_MAILBOX:
      return MAILBOX_TYPE_SCP_SECURE_MAILBOX;
    case MAILBOX_TYPE_AP_NS_LOW_PRIORITY_MAILBOX:
      return MAILBOX_TYPE_SCP_NS_LOW_PRIORITY_MAILBOX;
    case MAILBOX_TYPE_AP_NS_HIGH_PRIORITY_MAILBOX:
      return MAILBOX_TYPE_SCP_NS_HIGH_PRIORITY_MAILBOX;
    default:
      return MAILBOX_TYPE_INVALID_MAILBOX;
  }
}

size_t AmlMailbox::GetNumWords(size_t size) { return (size / 4 + ((size % 4) ? 1 : 0)); }

void AmlMailbox::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void AmlMailbox::DdkRelease() { delete this; }

zx_status_t AmlMailbox::MailboxSendCommand(const mailbox_channel_t* channel,
                                           const mailbox_data_buf_t* mdata) {
  if (!channel || !mdata) {
    return ZX_ERR_INVALID_ARGS;
  }

  mailbox_type_t rx_mailbox_id;
  if (MAILBOX_TYPE_INVALID_MAILBOX == (rx_mailbox_id = GetRxMailbox(channel->mailbox))) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock mailbox_lock(&mailbox_chan_lock_[channel->mailbox]);

  aml_mailbox_block_t* rx_mailbox = &vim2_mailbox_block[rx_mailbox_id];
  aml_mailbox_block_t* tx_mailbox = &vim2_mailbox_block[channel->mailbox];

  if (mdata->tx_size != 0) {
    ZX_DEBUG_ASSERT(mdata->tx_size % sizeof(uint32_t) == 0);

    size_t num = GetNumWords(mdata->tx_size);
    uint32_t* tx_payload = (uint32_t*)(mdata->tx_buffer);
    for (size_t i = 0; i < num; i++) {
      // AP writes parameters to Payload
      mailbox_payload_mmio_->Write32(tx_payload[i], tx_mailbox->payload_offset + (i << 2));
    }
  }

  // AP writes command to AP Mailbox
  mailbox_mmio_->Write32(mdata->cmd, tx_mailbox->set_offset);

  zx_status_t status = inth_[rx_mailbox_id].wait(nullptr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-mailbox: zx_interrupt_wait failed: %d", status);
    return status;
  }

  // AP reads the Payload to get requested information
  if (channel->rx_size != 0) {
    ZX_DEBUG_ASSERT(channel->rx_size % sizeof(uint32_t) == 0);

    size_t num = GetNumWords(channel->rx_size);
    uint32_t* rx_payload = (uint32_t*)(channel->rx_buffer);
    for (size_t i = 0; i < num; i++) {
      rx_payload[i] = mailbox_payload_mmio_->Read32(rx_mailbox->payload_offset + (i << 2));
    }
  }

  // AP writes to the Mailbox CLR register
  mailbox_mmio_->Write32(1, rx_mailbox->clr_offset);
  return ZX_OK;
}

zx_status_t AmlMailbox::Bind() {
  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_AMLOGIC},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_AMLOGIC_MAILBOX},
  };

  return DdkAdd(ddk::DeviceAddArgs("aml-mailbox").set_props(props));
}

zx_status_t AmlMailbox::InitPdev() {
  if (!pdev_.is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }

  // Map MMIOs
  zx_status_t status = pdev_.MapMmio(MMIO_MAILBOX, &mailbox_mmio_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-mailbox: could not map mailbox mmio: %d", status);
    return status;
  }

  status = pdev_.MapMmio(MMIO_MAILBOX_PAYLOAD, &mailbox_payload_mmio_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-mailbox: could not map payload mmio: %d", status);
    return status;
  }

  for (uint32_t i = 0; i < kNumMailboxes; i++) {
    status = pdev_.GetInterrupt(i, &inth_[i]);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-mailbox: could not map interrupt: %d", status);
      return status;
    }

    mtx_init(&mailbox_chan_lock_[i], mtx_plain);
  }

  return status;
}

zx_status_t AmlMailbox::Create(zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto mailbox_device = fbl::make_unique_checked<AmlMailbox>(&ac, parent);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = mailbox_device->InitPdev();
  if (status != ZX_OK) {
    return status;
  }

  status = mailbox_device->Bind();
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-mailbox driver failed to get added: %d", status);
    return status;
  } else {
    zxlogf(INFO, "aml-mailbox driver added");
  }

  // mailbox_device intentionally leaked as it is now held by DevMgr
  __UNUSED auto ptr = mailbox_device.release();

  return ZX_OK;
}

zx_status_t aml_mailbox_bind(void* ctx, zx_device_t* parent) {
  return mailbox::AmlMailbox::Create(parent);
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = aml_mailbox_bind;
  return ops;
}();

}  // namespace mailbox

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_mailbox, mailbox::driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S912),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_MAILBOX),
ZIRCON_DRIVER_END(aml_mailbox)
