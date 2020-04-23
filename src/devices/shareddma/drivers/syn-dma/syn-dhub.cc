// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "syn-dhub.h"

#include <lib/device-protocol/pdev.h>
#include <lib/zx/port.h>

#include <limits>
#include <utility>

#include <ddk/binding.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>
#include <soc/as370/as370-dhub-regs.h>
#include <soc/as370/as370-hw.h>

namespace {
constexpr uint64_t kPortKeyIrqMsg = 0x00;
constexpr uint64_t kPortShutdown = 0x01;
}  // namespace

namespace as370 {

std::unique_ptr<SynDhub> SynDhub::Create(zx_device_t* parent) {
  fbl::AllocChecker ac;

  ddk::PDev pdev = ddk::PDev(parent);
  std::optional<ddk::MmioBuffer> mmio;
  auto status = pdev.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not get MMIO %d", __func__, status);
    return nullptr;
  }
  auto ret = std::unique_ptr<SynDhub>(new (&ac) SynDhub(parent, *std::move(mmio)));
  if (!ac.check()) {
    return nullptr;
  }

  status = ret->Bind();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not bind %d", __func__, status);
    return nullptr;
  }

  return ret;
}

zx_status_t SynDhub::Bind() {
  ddk::PDev pdev = ddk::PDev(parent());
  auto status = pdev.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not obtain bti %d", __func__, status);
    return status;
  }

  status = pdev.GetInterrupt(0, &interrupt_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s GetInterrupt failed %d", __func__, status);
    return status;
  }

  status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s port create failed %d", __func__, status);
    return status;
  }

  status = interrupt_.bind(port_, kPortKeyIrqMsg, 0 /*options*/);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s interrupt bind failed %d", __func__, status);
    return status;
  }

  for (uint32_t i = 0; i < 32; ++i) {
    cell_CFG::Get(true, i).FromValue(0).set_DEPTH(1).WriteTo(&mmio_);
    cell_INTR0_mask::Get(true, i).FromValue(0).WriteTo(&mmio_);
  }

  auto cb = [](void* arg) -> int { return reinterpret_cast<SynDhub*>(arg)->Thread(); };
  int rc = thrd_create_with_name(&thread_, cb, this, "synaptics-dhub-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  zx_device_prop_t props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_SHARED_DMA},
  };
  status = DdkAdd("synaptics-dhub", DEVICE_ADD_ALLOW_MULTI_COMPOSITE, props, countof(props));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s DdkAdd failed %d", __func__, status);
    return status;
  }
  return ZX_OK;
}

int SynDhub::Thread() {
  while (1) {
    zx_port_packet_t packet = {};
    auto status = port_.wait(zx::time::infinite(), &packet);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s port wait failed: %d", __func__, status);
      return thrd_error;
    }
    zxlogf(TRACE, "dhub: msg on port key %lu", packet.key);
    if (packet.key == kPortShutdown) {
      zxlogf(INFO, "dhub: Synaptics Dhub DMA shutting down");
      return thrd_success;
    } else if (packet.key == kPortKeyIrqMsg) {
      auto interrupt_status = full::Get(true).ReadFrom(&mmio_).reg_value();
      uint32_t channel_id = __builtin_ctz(interrupt_status);
      Ack(channel_id);
      interrupt_.ack();
      if (channel_id == kDmaIdPdmW0) {
        ProcessIrq(kDmaIdPdmW1);  // PDM1 piggybacks on PDM0 interrupt.
      }
      ProcessIrq(channel_id);
      zxlogf(TRACE, "dhub: done channel id %u  status 0x%08X", channel_id, interrupt_status);
    }
  }
}

void SynDhub::Shutdown() {
  zx_port_packet packet = {kPortShutdown, ZX_PKT_TYPE_USER, ZX_OK, {}};
  zx_status_t status = port_.queue(&packet);
  ZX_ASSERT(status == ZX_OK);
  thrd_join(thread_, NULL);
  interrupt_.destroy();
}

zx_status_t SynDhub::SharedDmaSetNotifyCallback(uint32_t channel_id, const dma_notify_t* cb) {
  if (channel_id > DmaId::kDmaIdMax) {
    return ZX_ERR_INVALID_ARGS;
  }
  callback_[channel_id] = *cb;
  return ZX_OK;
}

zx_status_t SynDhub::SharedDmaInitializeAndGetBuffer(uint32_t channel_id, dma_type_t type,
                                                     uint32_t len, zx::vmo* out_vmo) {
  if (channel_id > DmaId::kDmaIdMax) {
    return ZX_ERR_INVALID_ARGS;
  }

  len = fbl::round_up<uint32_t, uint32_t>(len, kMtuSize * channel_info_[channel_id].dma_mtus);

  Init(channel_id);

  type_[channel_id] = type;
  auto status = zx::vmo::create_contiguous(bti_, len, 0, &dma_buffer_[channel_id]);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to allocate DMA buffer vmo %d", __FILE__, status);
    return status;
  }
  status = pinned_dma_buffer_[channel_id].Pin(dma_buffer_[channel_id], bti_,
                                              ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to pin DMA buffer vmo %d", __FILE__, status);
    return status;
  }
  if (pinned_dma_buffer_[channel_id].region_count() != 1) {
    zxlogf(ERROR, "%s buffer not contiguous", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }
  zx_paddr_t physical_address = pinned_dma_buffer_[channel_id].region(0).phys_addr;
  constexpr uint32_t minimum_alignment = 16;
  if ((physical_address % minimum_alignment)) {
    return ZX_ERR_INTERNAL;
  }
  if ((physical_address + len - 1) > std::numeric_limits<uint32_t>::max()) {
    return ZX_ERR_INVALID_ARGS;
  }
  SetBuffer(channel_id, physical_address, len);
  constexpr uint32_t rights =
      ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE;
  status = dma_buffer_[channel_id].duplicate(rights, out_vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to duplicate buffer vmo %d", __FILE__, status);
    return status;
  }

  // PDM1 piggybacks on PDM0 interrupt.
  triggers_interrupt_[channel_id] = channel_id != kDmaIdPdmW1;
  return ZX_OK;
}

// channel_id is validated before calling this function.
void SynDhub::Init(uint32_t channel_id) {
  const uint32_t fifo_cmd_id = 2 * channel_id;
  const uint32_t fifo_data_id = 2 * channel_id + 1;

  // Stop and clear FIFO for cmd and data.
  FiFo_START::Get(fifo_cmd_id).FromValue(0).set_EN(0).WriteTo(&mmio_);
  FiFo_CLEAR::Get(fifo_cmd_id).FromValue(0).set_EN(1).WriteTo(&mmio_);
  FiFo_START::Get(fifo_data_id).FromValue(0).set_EN(0).WriteTo(&mmio_);
  FiFo_CLEAR::Get(fifo_data_id).FromValue(0).set_EN(1).WriteTo(&mmio_);

  // Stop and configure channel.
  channelCtl_START::Get(channel_id).FromValue(0).WriteTo(&mmio_);
  channelCtl_CFG::Get(channel_id)
      .FromValue(0)
      .set_selfLoop(0)
      .set_QoS(0)
      .set_MTU(4)  // 128 bytes (2 ^ 4 x 8).
      .WriteTo(&mmio_);
  ZX_ASSERT(kMtuSize == 128);

  const uint32_t bank = channel_info_[channel_id].bank;
  const uint32_t base_cmd = bank * 512;
  const uint32_t base_data = bank * 512 + 32;
  constexpr uint32_t depth_cmd = 4;  // 4 x 8 = 32 bytes.

  // FIFO semaphores use cells with hub == false.

  // FIFO cmd configure and start.
  FiFo_CFG::Get(fifo_cmd_id).FromValue(0).set_BASE(base_cmd).WriteTo(&mmio_);
  cell_CFG::Get(false, fifo_cmd_id).FromValue(0).set_DEPTH(depth_cmd).WriteTo(&mmio_);
  FiFo_START::Get(fifo_cmd_id).FromValue(0).set_EN(1).WriteTo(&mmio_);

  // FIFO data configure and start.
  FiFo_CFG::Get(fifo_data_id).FromValue(0).set_BASE(base_data).WriteTo(&mmio_);
  cell_CFG::Get(false, fifo_data_id)
      .FromValue(0)
      .set_DEPTH(channel_info_[channel_id].fifo_data_depth)
      .WriteTo(&mmio_);
  FiFo_START::Get(fifo_data_id).FromValue(0).set_EN(1).WriteTo(&mmio_);

  // Channel configure and start.
  channelCtl_START::Get(channel_id).FromValue(0).set_EN(1).WriteTo(&mmio_);
  cell_CFG::Get(true, channel_id).FromValue(0).set_DEPTH(1).WriteTo(&mmio_);

  // Clear semaphore.
  auto active = full::Get(true).ReadFrom(&mmio_);
  if (active.reg_value()) {
    zxlogf(TRACE, "dhub: clearing active interrupts 0x%X", active.reg_value());
    full::Get(true).FromValue(active.reg_value()).WriteTo(&mmio_);
  }

  cell_INTR0_mask::Get(true, channel_id).FromValue(0).set_full(1).WriteTo(&mmio_);
}

void SynDhub::Enable(uint32_t channel_id, bool enable) {
  if (channel_id > DmaId::kDmaIdMax) {
    zxlogf(ERROR, "%s wrong channel id %u", __FILE__, channel_id);
    return;
  }

  enabled_[channel_id] = enable;

  // Clear the channel.
  uint32_t fifo_cmd_id = 2 * channel_id;
  uint32_t fifo_data_id = 2 * channel_id + 1;
  FiFo_START::Get(fifo_cmd_id).FromValue(0).set_EN(0).WriteTo(&mmio_);       // Stop cmd queue.
  channelCtl_START::Get(channel_id).FromValue(0).set_EN(0).WriteTo(&mmio_);  // Stop channel.
  channelCtl_CLEAR::Get(channel_id).FromValue(0).set_EN(1).WriteTo(&mmio_);  // Clear channel.
  while ((BUSY::Get().ReadFrom(&mmio_).ST() | PENDING::Get().ReadFrom(&mmio_).ST()) &
         (1 << channel_id)) {
  }  // Wait while busy.

  FiFo_START::Get(fifo_cmd_id).FromValue(0).set_EN(0).WriteTo(&mmio_);  // Stop cmd queue.
  FiFo_CLEAR::Get(fifo_cmd_id).FromValue(0).set_EN(1).WriteTo(&mmio_);  // Clear cmd queue.
  while (HBO_BUSY::Get().ReadFrom(&mmio_).ST() & (1 << fifo_cmd_id)) {
  }  // Wait while busy.

  FiFo_START::Get(fifo_data_id).FromValue(0).set_EN(0).WriteTo(&mmio_);  // Stop data queue.
  FiFo_CLEAR::Get(fifo_data_id).FromValue(0).set_EN(1).WriteTo(&mmio_);  // Clear data queue.
  while (HBO_BUSY::Get().ReadFrom(&mmio_).ST() & (1 << fifo_data_id)) {
  }  // Wait while busy.

  channelCtl_START::Get(channel_id).FromValue(0).set_EN(enable).WriteTo(&mmio_);  // Start channel.
  FiFo_START::Get(fifo_cmd_id).FromValue(0).set_EN(enable).WriteTo(&mmio_);       // Start FIFO.
  FiFo_START::Get(fifo_data_id).FromValue(0).set_EN(enable).WriteTo(&mmio_);      // Start FIFO.

  if (enable) {
    for (size_t i = 0; i < kConcurrentDmas; ++i) {
      StartDma(channel_id, triggers_interrupt_[channel_id]);
      if (i != kConcurrentDmas - 1) {
        fbl::AutoLock lock(&position_lock_);
        dma_current_[channel_id] += channel_info_[channel_id].dma_mtus * kMtuSize;
        // We must not wrap around on enable, if we do, something is wrong.
        ZX_ASSERT(dma_current_[channel_id] < dma_base_[channel_id] + dma_size_[channel_id]);
      }
    }
  }
}

uint32_t SynDhub::SharedDmaGetBufferPosition(uint32_t channel_id) {
  fbl::AutoLock lock(&position_lock_);
  return static_cast<uint32_t>(dma_current_[channel_id] - dma_base_[channel_id]);
}

uint32_t SynDhub::SharedDmaGetTransferSize(uint32_t channel_id) {
  return channel_info_[channel_id].dma_mtus * kMtuSize;
}

void SynDhub::StartDma(uint32_t channel_id, bool trigger_interrupt) {
  const uint32_t fifo_cmd_id = 2 * channel_id;
  constexpr bool producer = false;
  const uint16_t ptr = mmio_.Read<uint16_t>(0x1'0500 + (fifo_cmd_id << 2) + (producer << 7) + 2);
  const uint32_t base = (channel_info_[channel_id].bank * 2) << 8;

  uint32_t current = 0;
  {
    fbl::AutoLock lock(&position_lock_);
    current = static_cast<uint32_t>(dma_current_[channel_id]);
  }

  zxlogf(TRACE, "dhub: start channel id %u from 0x%X  amount 0x%X  ptr %u", channel_id, current,
         channel_info_[channel_id].dma_mtus * kMtuSize, ptr);

  // Write to SRAM.
  CommandAddress::Get(base + ptr * 8).FromValue(0).set_addr(current).WriteTo(&mmio_);
  CommandHeader::Get(base + ptr * 8)
      .FromValue(0)
      .set_interrupt(trigger_interrupt)
      .set_sizeMTU(1)
      .set_size(channel_info_[channel_id].dma_mtus)
      .WriteTo(&mmio_);
  PUSH::Get(false).FromValue(0).set_ID(fifo_cmd_id).set_delta(1).WriteTo(&mmio_);
}

void SynDhub::Ack(uint32_t channel_id) {
  if (channel_id >= DmaId::kDmaIdMax) {
    return;
  }
  auto interrupt_status = full::Get(true).ReadFrom(&mmio_).reg_value();
  if (!(interrupt_status & (1 << channel_id))) {
    zxlogf(TRACE, "dhub: ack interrupt wrong channel id %u  status 0x%X", channel_id,
           interrupt_status);
    return;
  }

  POP::Get(true).FromValue(0).set_delta(1).set_ID(channel_id).WriteTo(&mmio_);
  full::Get(true).ReadFrom(&mmio_).set_ST(1 << channel_id).WriteTo(&mmio_);
}

void SynDhub::ProcessIrq(uint32_t channel_id) {
  if (channel_id >= DmaId::kDmaIdMax) {
    return;
  }
  if (enabled_[channel_id]) {
    {
      fbl::AutoLock lock(&position_lock_);
      dma_current_[channel_id] += channel_info_[channel_id].dma_mtus * kMtuSize;
      if (dma_current_[channel_id] == dma_base_[channel_id] + dma_size_[channel_id]) {
        zxlogf(TRACE, "dhub: dma channel id %u  wraparound current 0x%lX  limit 0x%lX",
               channel_id, dma_current_[channel_id], dma_base_[channel_id] + dma_size_[channel_id]);
        dma_current_[channel_id] = dma_base_[channel_id];
      } else if (dma_current_[channel_id] > dma_base_[channel_id] + dma_size_[channel_id]) {
        zxlogf(ERROR, "dhub: dma channel id %u  current 0x%lX  exceeded 0x%lX", channel_id,
               dma_current_[channel_id], dma_base_[channel_id] + dma_size_[channel_id]);
      }
    }
    if (type_[channel_id] == DMA_TYPE_CYCLIC) {
      StartDma(channel_id, triggers_interrupt_[channel_id]);
    }
    if (callback_[channel_id].callback) {
      zxlogf(TRACE, "dhub: callback channel id %u", channel_id);
      callback_[channel_id].callback(callback_[channel_id].ctx, DMA_STATE_COMPLETED);
    }
  }
}

void SynDhub::SetBuffer(uint32_t channel_id, zx_paddr_t buf, size_t len) {
  fbl::AutoLock lock(&position_lock_);
  dma_base_[channel_id] = buf;
  dma_size_[channel_id] = static_cast<uint32_t>(len);
  dma_current_[channel_id] = dma_base_[channel_id];
  zxlogf(TRACE, "dhub: dma set to 0x%lX  size 0x%lX", dma_base_[channel_id], len);
}

}  // namespace as370

zx_status_t syn_dhub_bind(void* ctx, zx_device_t* parent) {
  auto dev = as370::SynDhub::Create(parent);
  // devmgr is now in charge of the memory for dev
  dev.release();
  return ZX_OK;
}

static constexpr zx_driver_ops_t syn_dhub_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = syn_dhub_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(syn_dhub, syn_dhub_driver_ops, "zircon", "0.1", 2)
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_SYNAPTICS),
  BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AS370_DHUB),
ZIRCON_DRIVER_END(syn_dhub)
    // clang-format on
