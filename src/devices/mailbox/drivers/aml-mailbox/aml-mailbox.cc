// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-mailbox.h"

#include <fuchsia/hardware/mailbox/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/fit/defer.h>
#include <lib/mmio/mmio-buffer.h>
#include <stdlib.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <cstdlib>

#include "src/devices/mailbox/drivers/aml-mailbox/aml_mailbox_bind.h"

namespace aml_mailbox {
#if AOCPU_MAILBOX_TEST
int AmlMailbox::TestAocpuThread() {
  zx_status_t status = ZX_OK;
  mailbox_channel_t* channel =
      reinterpret_cast<mailbox_channel_t*>(calloc(1, sizeof(mailbox_channel_t)));
  mailbox_data_buf_t* mdata =
      reinterpret_cast<mailbox_data_buf_t*>(calloc(1, sizeof(mailbox_data_buf_t)));
  mdata->tx_buffer = reinterpret_cast<const uint8_t*>(calloc(MBOX_FIFO_SIZE, sizeof(uint8_t)));
  channel->rx_buffer = reinterpret_cast<uint8_t*>(calloc(MBOX_FIFO_SIZE, sizeof(uint8_t)));
  uint8_t tx_data[MBOX_USER_LEN] = "ree2aocpu";
  uint8_t tx_length = sizeof(tx_data);
  channel->mailbox = MAILBOX_AOCPU;
  channel->rx_size = sizeof(tx_data);
  mdata->tx_size = tx_length;
  mdata->cmd = MBX_TEST_DEMO;
  mdata->tx_buffer = tx_data;
  /* sleep(5) has no special meaning. In order to make some debugging information at the end of
  the printed log during debugging, it is convenient to view the debugging information. */
  sleep(5);
  status = MailboxSendCommand(channel, mdata);
  if (status != 0) {
    zxlogf(INFO, "MailboxSendCommand failed!!");
    return status;
  }
  zxlogf(INFO, "Aocpu testing successfully!!");
  zxlogf(INFO, "channel->rx_buffer is %s", channel->rx_buffer);
  return status;
}
#endif

#if DSP_MAILBOX_TEST
int AmlMailbox::TestDspThread() {
  zx_status_t status = ZX_OK;
  mbox_uint sendbuf;
  uint32_t i, sumdata = 0;
  mailbox_channel_t* channel =
      reinterpret_cast<mailbox_channel_t*>(calloc(1, sizeof(mailbox_channel_t)));
  mailbox_data_buf_t* mdata =
      reinterpret_cast<mailbox_data_buf_t*>(calloc(1, sizeof(mailbox_data_buf_t)));
  mdata->tx_buffer = reinterpret_cast<const uint8_t*>(calloc(MBOX_FIFO_SIZE, sizeof(uint8_t)));
  channel->rx_buffer = reinterpret_cast<uint8_t*>(calloc(MBOX_FIFO_SIZE, sizeof(uint8_t)));

  sendbuf.uintcmd = 0x6;
  sumdata = 0;
  for (i = 0; i < RPCUINT_SIZE; i++) {
    sendbuf.data[i] = static_cast<uint8_t>(rand() % 0xff - 1);
    sumdata += sendbuf.data[i];
  }
  sendbuf.sumdata = sumdata;
  /* sleep(5) has no special meaning. In order to make some debugging information at the end of
  the printed log during debugging, it is convenient to view the debugging information. */
  sleep(5);
  channel->mailbox = MAILBOX_DSP;
  channel->rx_size = sizeof(sendbuf);
  mdata->tx_size = sizeof(sendbuf);
  mdata->cmd = MBX_CMD_RPCUINT_TESTA;
  mdata->tx_buffer = reinterpret_cast<const uint8_t*>(&sendbuf);
  sleep(5);
  status = MailboxSendCommand(channel, mdata);
  if (status != 0) {
    zxlogf(INFO, "MailboxSendCommand failed!!");
    return status;
  }
  memset(&sendbuf, 0, sizeof(sendbuf));
  memcpy(&sendbuf, channel->rx_buffer, sizeof(sendbuf));

  if (sendbuf.sumdata == (sumdata - 1))
    zxlogf(INFO, "Dsp testing successfully!!");
  return status;
}
#endif

void AmlMailbox::MboxFifoWrite(uint32_t offset, const uint8_t* from, long count) {
  int i = 0;
  long len = count / 4;
  long rlen = count % 4;
  uint32_t rdata = 0;
  auto p = reinterpret_cast<const uint32_t*>(from);

  while (len > 0) {
    mbox_wrmmio_.Write32(*p, offset + (4 * i));
    len--;
    i++;
    p++;
  }

  /* remainder data need use copy for no over mem size */
  if (rlen != 0) {
    memcpy(&rdata, p, rlen);
    mbox_wrmmio_.Write32(rdata, offset + (4 * i));
  }
}

void AmlMailbox::MboxFifoRead(uint32_t offset, uint8_t* to, long count) {
  int i = 0;
  long len = count / 4;
  long rlen = count % 4;
  uint32_t rdata = 0;
  auto p = reinterpret_cast<uint32_t*>(to);

  while (len > 0) {
    *p = mbox_rdmmio_.Read32(offset + (4 * i));
    len--;
    i++;
    p++;
  }

  /* remainder data need use copy for no over mem size */
  if (rlen != 0) {
    rdata = mbox_rdmmio_.Read32(offset + (4 * i));
    memcpy(p, &rdata, rlen);
  }
}

void AmlMailbox::MboxFifoClr(uint32_t offset, long count) {
  int i = 0;
  long len = count / 4 + count % 4;

  while (len > 0) {
    mbox_wrmmio_.Write32(0, offset + (4 * i));
    len--;
    i++;
  }
}

void AmlMailbox::MboxIrqClean(uint64_t mask) {
  uint64_t hstatus, lstatus;

  /* Determine if the mhu_irqmax value is 64, if it is 64, write data to the two registers */
  if (mhu_irqmax / MHUIRQ_MAXNUM_DEF == MHU_IRQ32) {
    hstatus = (mask >> MBOX_IRQSHIFT) & MBOX_IRQMASK;
    lstatus = mask & MBOX_IRQMASK;
    mbox_irqmmio_.Write32(static_cast<uint32_t>(lstatus), IRQ_CLR_OFFSETL(mhu_irqclr));
    mbox_irqmmio_.Write32(static_cast<uint32_t>(hstatus), IRQ_CLR_OFFSETH(mhu_irqclr));
  } else {
    mbox_irqmmio_.Write32((mask & MBOX_IRQMASK), IRQ_CLR_OFFSET(mhu_irqclr));
  }
}

void AmlMailbox::MboxAckIsrHandler(uint32_t mhu_id) {
  mailbox_channel_t* chan = nullptr;
  const uint8_t* data = nullptr;
  int channel;

  for (channel = 0; channel < CHANNEL_FIFO_MAX; channel++) {
    if (mbox_id[channel] == mhu_id)
      break;
  }

  if (channel >= CHANNEL_FIFO_MAX) {
    return;
  }

  chan = &channels[channel];
  data = reinterpret_cast<const uint8_t*>(chan->rx_buffer);
  if (data) {
    MboxFifoRead(PAYLOAD_OFFSET(mhu_id), const_cast<uint8_t*>(data), chan->rx_size);
    rx_flag[channel] = 1;
  }

  MboxFifoClr(PAYLOAD_OFFSET(mhu_id), MBOX_FIFO_SIZE);
  MboxIrqClean(IRQ_SENDACK_BIT(mhu_id));
}

void AmlMailbox::MboxIsrHandler(uint32_t mhu_id) {
  mailbox_channel_t* chan = nullptr;
  const uint8_t* data = nullptr;
  int channel;
  uint32_t status;

  for (channel = 0; channel < CHANNEL_FIFO_MAX; channel++) {
    if (mbox_id[channel] == mhu_id)
      break;
  }

  if (channel >= CHANNEL_FIFO_MAX) {
    return;
  }

  status = mbox_fstsmmio_.Read32(CTL_OFFSET(mhu_id));
  if (status) {
    chan = &channels[channel];
    data = reinterpret_cast<const uint8_t*>(chan->rx_buffer);
    if (data) {
      MboxFifoRead(PAYLOAD_OFFSET(mhu_id), const_cast<uint8_t*>(data), chan->rx_size);
      rx_flag[channel] = 1;
    }
  }

  MboxIrqClean(IRQ_REV_BIT(mhu_id));
  mbox_fclrmmio_.Write32(~0, CTL_OFFSET(mhu_id));
}

uint64_t AmlMailbox::MboxIrqStatus() {
  uint64_t status, hstatus, lstatus;

  /* Determine if the mhu_irqmax value is 64, if it is 64, read the data from the two registers */
  if (mhu_irqmax / MHUIRQ_MAXNUM_DEF == MHU_IRQ32) {
    lstatus = mbox_irqmmio_.Read32(IRQ_STS_OFFSETL(mhu_irqctlr));
    hstatus = mbox_irqmmio_.Read32(IRQ_STS_OFFSETH(mhu_irqctlr));
    status = (hstatus << MBOX_IRQSHIFT) | (lstatus & MBOX_IRQMASK);
  } else {
    status = mbox_irqmmio_.Read32(IRQ_STS_OFFSET(mhu_irqctlr));
  }
  return status;
}

int AmlMailbox::IrqThread() {
  zx_status_t status;
  uint32_t irqmax = mhu_irqmax;
  uint64_t irqstatus, prestatus = 0;
  uint32_t outcnt;
  uint64_t bit = 1;
  uint32_t i;

  while (1) {
    status = irq_.wait(nullptr);
    if (status != ZX_OK) {
      zxlogf(ERROR, "irq wait failed, retcode %s", zx_status_get_string(status));
      return status;
    }

    outcnt = irqmax;
    irqstatus = MboxIrqStatus();
    while (irqstatus && (outcnt != 0)) {
      for (i = 0; i < irqmax; i++) {
        if (irqstatus & (bit << i)) {
          if (i % 2)
            MboxAckIsrHandler(i / 2);
          else
            MboxIsrHandler(i / 2);
        }
      }

      prestatus = irqstatus;
      irqstatus = MboxIrqStatus();
      irqstatus = (irqstatus | prestatus) ^ prestatus;
      outcnt--;
    }
  }

  return ZX_OK;
}

void AmlMailbox::MailboxSendData(int idx, uint32_t cmd, uint8_t* data, int tx_size) {
  int mhu_id = mbox_id[idx];
  MboxFifoWrite(PAYLOAD_OFFSET(mhu_id), data, tx_size);
  mbox_fsetmmio_.Write32(cmd, CTL_OFFSET(mhu_id));
}

zx_status_t AmlMailbox::AocpuMboxMessageWrite(const mailbox_channel_t* channel,
                                              const mailbox_data_buf_t* mdata) {
  uint8_t* data;
  uint32_t new_cmd;
  uint8_t tx_size;
  tx_size = static_cast<uint8_t>(mdata->tx_size) + MBOX_USER_CMD_LEN;
  if (tx_size > MBOX_USER_SIZE) {
    zxlogf(INFO, "Msg len %d over range", tx_size);
    return -EINVAL;
  }

  data = reinterpret_cast<uint8_t*>(calloc(tx_size + MBOX_HEAD_SIZE, sizeof(uint8_t)));
  memset(data, 0, tx_size + MBOX_HEAD_SIZE);

  new_cmd = (mdata->cmd & MBCMD_MASK) | ((tx_size + MBOX_HEAD_SIZE) & MBSIZE_MASK) << MBSIZE_SHIFT |
            static_cast<uint32_t>(SYNC_CMD_TAG);

  memcpy(data + MBOX_HEAD_SIZE, mdata->tx_buffer, tx_size);
  MailboxSendData(channel->mailbox, new_cmd, data, tx_size + MBOX_HEAD_SIZE);

  return ZX_OK;
}

int AmlMailbox::ToSendIdx(int idx, bool send) {
  if (send)
    return (idx % 2) ? idx : idx + 1;
  return (idx % 2) ? idx - 1 : idx;
}

zx_status_t AmlMailbox::DspMboxMessageWrite(const mailbox_channel_t* channel,
                                            const mailbox_data_buf_t* mdata) {
  uint8_t* data;
  uint32_t new_cmd;
  uint8_t tx_size;
  tx_size = static_cast<uint8_t>(mdata->tx_size) + MBOX_USER_CMD_LEN;
  if (tx_size > MBOX_USER_MAX_LEN || tx_size < MBOX_USER_CMD_LEN) {
    zxlogf(INFO, "Msg len %d over range", tx_size);
    return -EINVAL;
  }

  tx_size = tx_size + MBOX_HEAD_SIZE - MBOX_USER_CMD_LEN;
  data = reinterpret_cast<uint8_t*>(calloc(tx_size, sizeof(uint8_t)));
  memset(data, 0, tx_size);
  memcpy(data + MBOX_HEAD_SIZE, mdata->tx_buffer, tx_size - MBOX_HEAD_SIZE);
  new_cmd = CMD_SHIFT(mdata->cmd) | SIZE_SHIFT(tx_size) | SYNC_SHIFT(ASYNC_CMD);
  MailboxSendData(ToSendIdx(channel->mailbox, true), new_cmd, data, tx_size);

  return ZX_OK;
}

void AmlMailbox::AocpuMboxMessageRead(mailbox_channel_t* channel, const mailbox_data_buf_t* mdata) {
  mailbox_channel_t* mhu_chan;
  size_t rx_size;

  mhu_chan = &channels[channel->mailbox];
  while (rx_flag[channel->mailbox] == 0)
    ;

  rx_size = channel->rx_size + MBOX_USER_CMD_LEN;
  memcpy(const_cast<uint8_t*>(channel->rx_buffer), mhu_chan->rx_buffer + MBOX_HEAD_SIZE, rx_size);
  channel->rx_size = rx_size;

  rx_flag[channel->mailbox] = 0;
  memset(const_cast<uint8_t*>(channels[channel->mailbox].rx_buffer), 0, MBOX_FIFO_SIZE);
}

void AmlMailbox::DspMboxMessageRead(mailbox_channel_t* channel, const mailbox_data_buf_t* mdata) {
  mailbox_channel_t* mhu_chan;
  mhu_chan = &channels[channel->mailbox];
  while (rx_flag[channel->mailbox] == 0)
    ;

  memcpy(const_cast<uint8_t*>(channel->rx_buffer), mhu_chan->rx_buffer + MBOX_HEAD_SIZE,
         channel->rx_size);

  rx_flag[channel->mailbox] = 0;
  memset(const_cast<uint8_t*>(channels[channel->mailbox].rx_buffer), 0, MBOX_FIFO_SIZE);
  rx_flag[ToSendIdx(channel->mailbox, true)] = 0;
  memset(const_cast<uint8_t*>(channels[ToSendIdx(channel->mailbox, true)].rx_buffer), 0,
         MBOX_FIFO_SIZE);
}

zx_status_t AmlMailbox::MailboxSendCommand(const mailbox_channel_t* channel,
                                           const mailbox_data_buf_t* mdata) {
  zx_status_t status = ZX_OK;
  switch (channel->mailbox) {
    case MAILBOX_AOCPU:
      status = AocpuMboxMessageWrite(channel, mdata);
      if (status != 0) {
        zxlogf(INFO, "aocpumbox_message_write failed!!");
      }

      if (channel->rx_size)
        AocpuMboxMessageRead(const_cast<mailbox_channel_t*>(channel), mdata);
      break;

    case MAILBOX_DSP:
      status = DspMboxMessageWrite(channel, mdata);
      if (status != 0) {
        zxlogf(INFO, "dspmbox_message_write failed!!");
      }

      if (channel->rx_size)
        DspMboxMessageRead(const_cast<mailbox_channel_t*>(channel), mdata);
      break;
  }

  return status;
}

void AmlMailbox::ShutDown() { irq_.destroy(); }

void AmlMailbox::DdkUnbind(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

void AmlMailbox::DdkRelease() { delete this; }

zx_status_t AmlMailbox::Init() {
  auto cleanup = fit::defer([&]() { ShutDown(); });
  uint32_t idx;
  mailbox_channel_t* mhu_chan;
  mhu_irqctlr = 0;
  mhu_irqclr = 0;
  mhu_irqmax = MHUIRQ_MAXNUM_DEF;
  num_chans = 4;
  mbox_id[0] = 2;
  mbox_id[1] = 3;
  mbox_id[2] = 0;
  mbox_id[3] = 1;
  memset(rx_flag, 0, sizeof(rx_flag));

  channels = reinterpret_cast<mailbox_channel_t*>(calloc(num_chans, sizeof(mailbox_channel_t)));
  for (idx = 0; idx < num_chans; idx++) {
    mhu_chan = &channels[idx];
    mhu_chan->mailbox = static_cast<mailbox_type_t>(idx);
    mhu_chan->rx_buffer = reinterpret_cast<uint8_t*>(calloc(MBOX_FIFO_SIZE, sizeof(uint8_t)));
    mhu_chan->rx_size = MBOX_FIFO_SIZE;
  }

  auto thunkone = [](void* arg) -> int { return reinterpret_cast<AmlMailbox*>(arg)->IrqThread(); };
  int rcone = thrd_create_with_name(&irq_thread_, thunkone, this, "mailbox-irq");
  if (rcone != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

#if AOCPU_MAILBOX_TEST
  thrd_t aocpu_test_thread;
  auto aocpu_thunk = [](void* arg) -> int {
    return reinterpret_cast<AmlMailbox*>(arg)->TestAocpuThread();
  };
  int rctwo = thrd_create_with_name(&aocpu_test_thread, aocpu_thunk, this, "aocpu-mailbox-test");
  if (rctwo != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
#endif

#if DSP_MAILBOX_TEST
  thrd_t dsp_test_thread;
  auto dsp_thunk = [](void* arg) -> int {
    return reinterpret_cast<AmlMailbox*>(arg)->TestDspThread();
  };
  int rcthree = thrd_create_with_name(&dsp_test_thread, dsp_thunk, this, "dsp-mailbox-test");
  if (rcthree != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
#endif

  cleanup.cancel();
  return ZX_OK;
}

zx_status_t AmlMailbox::Create(void* ctx, zx_device_t* parent) {
  zx_status_t status;
  fbl::AllocChecker ac;
  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "Failed to get ZX_PROTOCOL_PDEV");
    return ZX_ERR_NO_RESOURCES;
  }

  pdev_device_info_t info;
  if ((status = pdev.GetDeviceInfo(&info)) != ZX_OK) {
    zxlogf(ERROR, "pdev_get_device_info failed");
    return ZX_ERR_NOT_SUPPORTED;
  }

  std::optional<ddk::MmioBuffer> mbox_wrmmio;
  if ((status = pdev.MapMmio(0, &mbox_wrmmio)) != ZX_OK) {
    zxlogf(ERROR, "pdev_map_mmio_buffer wr failed %s", zx_status_get_string(status));
    return status;
  }

  std::optional<ddk::MmioBuffer> mbox_rdmmio;
  if ((status = pdev.MapMmio(1, &mbox_rdmmio)) != ZX_OK) {
    zxlogf(ERROR, "pdev_map_mmio_buffer rd failed %s", zx_status_get_string(status));
    return status;
  }

  std::optional<ddk::MmioBuffer> mbox_fsetmmio;
  if ((status = pdev.MapMmio(2, &mbox_fsetmmio)) != ZX_OK) {
    zxlogf(ERROR, "pdev_map_mmio_buffer fset failed %s", zx_status_get_string(status));
    return status;
  }

  std::optional<ddk::MmioBuffer> mbox_fclrmmio;
  if ((status = pdev.MapMmio(3, &mbox_fclrmmio)) != ZX_OK) {
    zxlogf(ERROR, "pdev_map_mmio_buffer fclr failed %s", zx_status_get_string(status));
    return status;
  }

  std::optional<ddk::MmioBuffer> mbox_fstsmmio;
  if ((status = pdev.MapMmio(4, &mbox_fstsmmio)) != ZX_OK) {
    zxlogf(ERROR, "pdev_map_mmio_buffer fsts failed %s", zx_status_get_string(status));
    return status;
  }

  std::optional<ddk::MmioBuffer> mbox_irqmmio;
  if ((status = pdev.MapMmio(5, &mbox_irqmmio)) != ZX_OK) {
    zxlogf(ERROR, "pdev_map_mmio_buffer irq failed %s", zx_status_get_string(status));
    return status;
  }

  zx::interrupt irq;
  if ((status = pdev.GetInterrupt(0, &irq)) != ZX_OK) {
    return status;
  }

  auto dev = fbl::make_unique_checked<AmlMailbox>(
      &ac, parent, *std::move(mbox_wrmmio), *std::move(mbox_rdmmio), *std::move(mbox_fsetmmio),
      *std::move(mbox_fclrmmio), *std::move(mbox_fstsmmio), *std::move(mbox_irqmmio), irq);
  if (!ac.check()) {
    zxlogf(ERROR, "ZX_ERR_NO_MEMORY");
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = dev->DdkAdd("aml-mailbox")) != ZX_OK) {
    zxlogf(ERROR, "DdkAdd failed: %s", zx_status_get_string(status));
    dev->ShutDown();
    return status;
  }

  // Devmgr is now in charge of the memory for dev.
  auto ptr = dev.release();
  return ptr->Init();
}

static zx_driver_ops_t mailbox_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = AmlMailbox::Create,
};

}  // namespace aml_mailbox

ZIRCON_DRIVER(aml_mailbox, aml_mailbox::mailbox_driver_ops, "zircon", "0.1");
