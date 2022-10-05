// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/mailbox/drivers/aml-mailbox/aml-mailbox.h"

#include <fidl/fuchsia.hardware.mailbox/cpp/markers.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fit/defer.h>
#include <lib/mmio/mmio-buffer.h>
#include <stdlib.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <cstdlib>
#include <vector>

#include "src/devices/mailbox/drivers/aml-mailbox/aml_mailbox_bind.h"

namespace {

struct MboxData {
  uint32_t status;
  uint64_t task;
  uint64_t complete;
  uint64_t ullclt;
  uint8_t data[MBOX_DATA_SIZE];
} __attribute__((packed));

constexpr uint8_t kMhuIrqCtrl = 0;
constexpr uint8_t kMhuIrqClr = 0;
constexpr uint8_t kMhuIrqMax = kMhuIrqMaxNumDef;
constexpr uint8_t kDefaultValue = 0;

inline uint8_t ToSendIdx(uint8_t idx) { return (idx % 2) ? idx : idx + 1; }

}  // namespace

namespace aml_mailbox {

void AmlMailbox::MboxFifoWrite(uint32_t offset, const uint8_t* from, uint8_t count) {
  auto p = reinterpret_cast<const uint32_t*>(from);
  size_t i = 0;
  for (i = 0; i < ((count / 4) * 4); i += sizeof(uint32_t)) {
    mbox_wrmmio_.Write32(*p, offset + i);
    p++;
  }

  /* remainder data need use copy for no over mem size */
  if (count % 4 != 0) {
    uint32_t rdata = 0;
    memcpy(&rdata, p, count % 4);
    mbox_wrmmio_.Write32(rdata, offset + i);
  }
}

void AmlMailbox::MboxFifoClr(uint32_t offset) {
  for (size_t i = 0; i < kMboxFifoSize; i += sizeof(uint32_t)) {
    mbox_wrmmio_.Write32(0, offset + i);
  }
}

void AmlMailbox::MboxIrqClean(uint64_t mask) {
  /* Determine if the mhu_irqmax value is 64, if it is 64, write data to the two registers */
  if (kMhuIrqMax / kMhuIrqMaxNumDef == kMhuIrq32) {
    uint32_t hstatus = (mask >> kMboxIrqShift) & UINT32_MAX;
    uint32_t lstatus = mask & UINT32_MAX;
    mbox_irqmmio_.Write32(lstatus, IRQ_CLR_OFFSETL(kMhuIrqClr));
    mbox_irqmmio_.Write32(hstatus, IRQ_CLR_OFFSETH(kMhuIrqClr));
  } else {
    mbox_irqmmio_.Write32((mask & UINT32_MAX), IRQ_CLR_OFFSET(kMhuIrqClr));
  }
}

void AmlMailbox::MboxAckIsrHandler(uint32_t mhu_id) {
  size_t channel = 0;
  for (channel = 0; channel < kMboxMax; channel++) {
    if (mbox_id_[channel] == mhu_id)
      break;
  }

  if (channel >= kMboxMax) {
    return;
  }

  mbox_rdmmio_.ReadBuffer(PAYLOAD_OFFSET(mhu_id), channels_[channel].data(),
                          channels_[channel].size());
  rx_flag_[channel] = 1;

  MboxFifoClr(PAYLOAD_OFFSET(mhu_id));
  MboxIrqClean(IRQ_SENDACK_BIT(mhu_id));
}

void AmlMailbox::MboxIsrHandler(uint32_t mhu_id) {
  size_t channel = 0;
  for (channel = 0; channel < kMboxMax; channel++) {
    if (mbox_id_[channel] == mhu_id)
      break;
  }

  if (channel >= kMboxMax) {
    return;
  }

  uint32_t status = mbox_fstsmmio_.Read32(CTL_OFFSET(mhu_id));
  if (status) {
    mbox_rdmmio_.ReadBuffer(PAYLOAD_OFFSET(mhu_id), channels_[channel].data(),
                            channels_[channel].size());
    rx_flag_[channel] = 1;
  }

  MboxIrqClean(IRQ_REV_BIT(mhu_id));
  mbox_fclrmmio_.Write32(~0, CTL_OFFSET(mhu_id));
}

uint64_t AmlMailbox::MboxIrqStatus() {
  uint64_t status = 0;
  /* Determine if the mhu_irqmax value is 64, if it is 64, read the data from the two registers */
  if (kMhuIrqMax / kMhuIrqMaxNumDef == kMhuIrq32) {
    uint64_t lstatus = mbox_irqmmio_.Read32(IRQ_STS_OFFSETL(kMhuIrqCtrl));
    uint64_t hstatus = mbox_irqmmio_.Read32(IRQ_STS_OFFSETH(kMhuIrqCtrl));
    status = (hstatus << kMboxIrqShift) | (lstatus & UINT32_MAX);
  } else {
    status = mbox_irqmmio_.Read32(IRQ_STS_OFFSET(kMhuIrqCtrl));
  }
  return status;
}

int AmlMailbox::IrqThread() {
  uint32_t irqmax = kMhuIrqMax;
  uint64_t prestatus = 0;
  uint64_t bit = 1;

  while (1) {
    zx_status_t status = irq_.wait(nullptr);
    if (status != ZX_OK) {
      zxlogf(ERROR, "irq wait failed, retcode %s", zx_status_get_string(status));
      return status;
    }

    uint32_t outcnt = irqmax;
    uint64_t irqstatus = MboxIrqStatus();
    while (irqstatus && (outcnt != 0)) {
      for (uint32_t i = 0; i < irqmax; i++) {
        if (irqstatus & (bit << i)) {
          if (i % 2)
            MboxAckIsrHandler(i / 2);
          else
            MboxIsrHandler(i / 2);
        }
      }

      prestatus = irqstatus;
      irqstatus = MboxIrqStatus();
      irqstatus &= ~prestatus;
      outcnt--;
    }
  }

  return ZX_OK;
}

void AmlMailbox::MailboxSendData(uint8_t idx, uint32_t cmd, uint8_t* data, uint8_t tx_size) {
  uint8_t mhu_id = mbox_id_[idx];
  MboxFifoWrite(PAYLOAD_OFFSET(mhu_id), data, tx_size);
  mbox_fsetmmio_.Write32(cmd, CTL_OFFSET(mhu_id));
}

zx_status_t AmlMailbox::AocpuMboxMessageWrite(uint8_t channel, FidlMailbox::wire::MboxTx* mdata) {
  uint8_t tx_size = static_cast<uint8_t>(mdata->tx_buffer.count());
  if (tx_size > (kMboxFifoSize - kMboxUserCmdLen)) {
    zxlogf(ERROR, "Msg len %d over range", tx_size);
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint32_t new_cmd = (mdata->cmd & UINT32_MAX) | SIZE_SHIFT(tx_size + kMboxHeadSize) |
                     static_cast<uint32_t>(SYNC_CMD_TAG);
  /* This uses a mailbox communication mechanism to send data once. The complete data structure
   * should be: uint32_t status + uint64_t taskid + uint64_t complete+uint64_t ullclt + the actual
   * data sent. According to the mechanism, when ARM sends data, the status needs to be set to 0.
   * The taskid is set to 0, complete is set to 0, and ullclt is set to 0. Therefore, the first 28
   * bytes of the data array are set to 0, and the actual sent data starts from data[28]. */
  struct MboxData mboxdata = {
      .status = kDefaultValue,
      .task = kDefaultValue,
      .complete = kDefaultValue,
      .ullclt = kDefaultValue,
  };
  memset(mboxdata.data, 0, sizeof(mboxdata.data));
  memcpy(mboxdata.data, &mdata->tx_buffer[0],
         mdata->tx_buffer.count() * sizeof(mdata->tx_buffer[0]));
  MailboxSendData(channel, new_cmd, reinterpret_cast<uint8_t*>(&mboxdata), tx_size + kMboxHeadSize);
  return ZX_OK;
}

zx_status_t AmlMailbox::DspMboxMessageWrite(uint8_t channel, FidlMailbox::wire::MboxTx* mdata) {
  uint8_t tx_size = static_cast<uint8_t>(mdata->tx_buffer.count()) + kMboxUserCmdLen;
  if (tx_size > MBOX_USER_MAX_LEN) {
    zxlogf(ERROR, "Msg len %d over range", tx_size);
    return ZX_ERR_OUT_OF_RANGE;
  }

  tx_size = tx_size + kMboxHeadSize - kMboxUserCmdLen;
  uint32_t new_cmd =
      (mdata->cmd & UINT32_MAX) | SIZE_SHIFT(tx_size) | static_cast<uint32_t>(ASYNC_CMD_TAG);

  /* This uses a mailbox communication mechanism to send data once. The complete data structure
   * should be: uint32_t status + uint64_t taskid + uint64_t complete+uint64_t ullclt + the actual
   * data sent. According to the mechanism, when ARM sends data, the status needs to be set to 0.
   * The taskid is set to 0, complete is set to 0, and ullclt is set to 0. Therefore, the first 28
   * bytes of the data array are set to 0, and the actual sent data starts from data[28]. */
  struct MboxData mboxdata = {
      .status = kDefaultValue,
      .task = kDefaultValue,
      .complete = kDefaultValue,
      .ullclt = kDefaultValue,
  };
  memset(mboxdata.data, 0, sizeof(mboxdata.data));
  memcpy(mboxdata.data, &mdata->tx_buffer[0],
         mdata->tx_buffer.count() * sizeof(mdata->tx_buffer[0]));
  MailboxSendData(ToSendIdx(channel), new_cmd, reinterpret_cast<uint8_t*>(&mboxdata), tx_size);
  return ZX_OK;
}

zx_status_t AmlMailbox::ScpiMessageWrite(uint8_t channel, FidlMailbox::wire::MboxTx* mdata) {
  uint8_t tx_size = static_cast<uint8_t>(mdata->tx_buffer.count()) + kMboxHeadSize;
  uint32_t new_cmd =
      (mdata->cmd & UINT32_MAX) | SIZE_SHIFT(tx_size) | static_cast<uint32_t>(SYNC_CMD_TAG);

  /* This uses a mailbox communication mechanism to send data once. The complete data structure
   * should be: uint32_t status + uint64_t taskid + uint64_t complete+uint64_t ullclt + the actual
   * data sent. According to the mechanism, when ARM sends data, the status needs to be set to 0.
   * The taskid is set to 0, complete is set to 0, and ullclt is set to 0. Therefore, the first 28
   * bytes of the data array are set to 0, and the actual sent data starts from data[28]. */
  struct MboxData mboxdata = {
      .status = kDefaultValue,
      .task = kDefaultValue,
      .complete = kDefaultValue,
      .ullclt = kDefaultValue,
  };
  memset(mboxdata.data, 0, sizeof(mboxdata.data));
  memcpy(mboxdata.data, &mdata->tx_buffer[0],
         mdata->tx_buffer.count() * sizeof(mdata->tx_buffer[0]));

  MailboxSendData(channel, new_cmd, reinterpret_cast<uint8_t*>(&mboxdata), tx_size);
  return ZX_OK;
}

void AmlMailbox::ReceiveData(ReceiveDataRequestView request,
                             ReceiveDataCompleter::Sync& completer) {
  using fuchsia_hardware_mailbox::wire::DeviceReceiveDataResponse;
  uint8_t channel = request->channel;

  /* A loop will only occur if the following two conditions are met: 1. Aocpu and DSP do not respond
   * after receiving the command; 2. An exception occurs in the driver's interrupt handling
   * function, and rx_flag_[channel] is not set to 1 after receiving the interrupt */
  while (rx_flag_[channel] == 0)
    ;

  fidl::Arena allocator;
  DeviceReceiveDataResponse response;
  response.mdata.rx_buffer.Allocate(allocator, request->rx_len);
  /* This is a mailbox communication mechanism to receive data once, and the complete data structure
   * should be: uint32_t status + uint64_t taskid + uint64_t complete+uint64_t ullclt + actual data
   * received, according to the mechanism, the data required by the user is from
   * channels_[channel][28] started. */
  memcpy(&response.mdata.rx_buffer[0], &channels_[channel][kMboxHeadSize],
         response.mdata.rx_buffer.count() * sizeof(response.mdata.rx_buffer[0]));

  if ((channel == kMailboxAocpu) || (channel == kMailboxScpi)) {
    rx_flag_[channel] = 0;
    memset(channels_[channel].data(), 0, channels_[channel].size());
  }

  if (channel == kMailboxDsp) {
    rx_flag_[channel] = 0;
    memset(channels_[channel].data(), 0, channels_[channel].size());

    rx_flag_[ToSendIdx(channel)] = 0;
    memset(channels_[ToSendIdx(channel)].data(), 0, channels_[ToSendIdx(channel)].size());
  }

  completer.Reply(::fit::ok(&response));
}

zx_status_t AmlMailbox::SendCommand(uint8_t channel, FidlMailbox::wire::MboxTx* mdata) {
  zx_status_t status = ZX_OK;
  switch (channel) {
    case kMailboxAocpu:
      status = AocpuMboxMessageWrite(channel, mdata);
      if (status != ZX_OK) {
        zxlogf(ERROR, "aocpumbox_message_write failed %s", zx_status_get_string(status));
      }
      break;

    case kMailboxDsp:
      status = DspMboxMessageWrite(channel, mdata);
      if (status != ZX_OK) {
        zxlogf(ERROR, "dspmbox_message_write failed %s", zx_status_get_string(status));
      }
      break;

    case kMailboxScpi:
      status = ScpiMessageWrite(channel, mdata);
      if (status != ZX_OK) {
        zxlogf(ERROR, "scpi_message_write failed %s", zx_status_get_string(status));
      }
      break;

    default:
      zxlogf(ERROR, "The value of channel is not valid");
      status = ZX_ERR_INVALID_ARGS;
  }

  return status;
}

void AmlMailbox::SendCommand(SendCommandRequestView request,
                             SendCommandCompleter::Sync& completer) {
  FidlMailbox::wire::MboxTx data = request->mdata;
  uint8_t channel = request->channel;

  zx_status_t status = SendCommand(channel, &data);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess();
  }
}

void AmlMailbox::ShutDown() { irq_.destroy(); }

void AmlMailbox::DdkUnbind(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

void AmlMailbox::DdkRelease() { delete this; }

zx_status_t AmlMailbox::Init() {
  mbox_id_[0] = 2;
  mbox_id_[1] = 3;
  mbox_id_[2] = 0;
  mbox_id_[3] = 1;
  memset(rx_flag_, 0, sizeof(rx_flag_));

  for (uint32_t idx = 0; idx < kMboxMax; idx++) {
    memset(channels_[idx].data(), 0, channels_[idx].size());
  }

  auto thunkone = [](void* arg) -> int { return reinterpret_cast<AmlMailbox*>(arg)->IrqThread(); };
  int rcone = thrd_create_with_name(&irq_thread_, thunkone, this, "mailbox-irq");
  if (rcone != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

AmlMailbox::~AmlMailbox() {
  if (irq_.is_valid()) {
    irq_.destroy();
  }
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
    zxlogf(ERROR, "pdev_get_device_info failed %s", zx_status_get_string(status));
    return ZX_ERR_NOT_SUPPORTED;
  }

  std::optional<fdf::MmioBuffer> mbox_wrmmio;
  if ((status = pdev.MapMmio(0, &mbox_wrmmio)) != ZX_OK) {
    zxlogf(ERROR, "pdev_map_mmio_buffer wr failed %s", zx_status_get_string(status));
    return status;
  }

  std::optional<fdf::MmioBuffer> mbox_rdmmio;
  if ((status = pdev.MapMmio(1, &mbox_rdmmio)) != ZX_OK) {
    zxlogf(ERROR, "pdev_map_mmio_buffer rd failed %s", zx_status_get_string(status));
    return status;
  }

  std::optional<fdf::MmioBuffer> mbox_fsetmmio;
  if ((status = pdev.MapMmio(2, &mbox_fsetmmio)) != ZX_OK) {
    zxlogf(ERROR, "pdev_map_mmio_buffer fset failed %s", zx_status_get_string(status));
    return status;
  }

  std::optional<fdf::MmioBuffer> mbox_fclrmmio;
  if ((status = pdev.MapMmio(3, &mbox_fclrmmio)) != ZX_OK) {
    zxlogf(ERROR, "pdev_map_mmio_buffer fclr failed %s", zx_status_get_string(status));
    return status;
  }

  std::optional<fdf::MmioBuffer> mbox_fstsmmio;
  if ((status = pdev.MapMmio(4, &mbox_fstsmmio)) != ZX_OK) {
    zxlogf(ERROR, "pdev_map_mmio_buffer fsts failed %s", zx_status_get_string(status));
    return status;
  }

  std::optional<fdf::MmioBuffer> mbox_irqmmio;
  if ((status = pdev.MapMmio(5, &mbox_irqmmio)) != ZX_OK) {
    zxlogf(ERROR, "pdev_map_mmio_buffer irq failed %s", zx_status_get_string(status));
    return status;
  }

  zx::interrupt irq;
  if ((status = pdev.GetInterrupt(0, &irq)) != ZX_OK) {
    return status;
  }

  async_dispatcher_t* dispatcher = fdf::Dispatcher::GetCurrent()->async_dispatcher();
  auto dev = fbl::make_unique_checked<AmlMailbox>(
      &ac, parent, *std::move(mbox_wrmmio), *std::move(mbox_rdmmio), *std::move(mbox_fsetmmio),
      *std::move(mbox_fclrmmio), *std::move(mbox_fstsmmio), *std::move(mbox_irqmmio),
      std::move(irq), dispatcher);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = dev->Init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "AmlMailbox initialization failed %s", zx_status_get_string(status));
  }

  dev->outgoing_.emplace(dev->dispatcher_);
  dev->outgoing_->svc_dir()->AddEntry(
      fidl::DiscoverableProtocolName<fuchsia_hardware_mailbox::Device>,
      fbl::MakeRefCounted<fs::Service>(
          [dev = dev.get()](fidl::ServerEnd<fuchsia_hardware_mailbox::Device> request) mutable {
            fidl::BindServer(dev->dispatcher_, std::move(request), dev);
            return ZX_OK;
          }));

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  status = dev->outgoing_->Serve(std::move(endpoints->server));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to service the outoing directory %s", zx_status_get_string(status));
    return status;
  }

  std::array offers = {
      fidl::DiscoverableProtocolName<fuchsia_hardware_mailbox::Device>,
  };

  status = dev->DdkAdd(ddk::DeviceAddArgs("aml-mailbox")
                           .set_flags(DEVICE_ADD_MUST_ISOLATE)
                           .set_fidl_protocol_offers(offers)
                           .set_outgoing_dir(endpoints->client.TakeChannel())
                           .set_proto_id(ZX_PROTOCOL_AML_MAILBOX));
  if (status == ZX_OK) {
    // Devmgr is now in charge of the memory for dev.
    __UNUSED auto* dummy = dev.release();
    return ZX_OK;
  } else {
    zxlogf(ERROR, "DdkAdd failed: %s", zx_status_get_string(status));
    dev->ShutDown();
    return status;
  }
}

static zx_driver_ops_t mailbox_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = AmlMailbox::Create,
};

}  // namespace aml_mailbox

ZIRCON_DRIVER(aml_mailbox, aml_mailbox::mailbox_driver_ops, "zircon", "0.1");
