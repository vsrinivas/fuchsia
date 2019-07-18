// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <utility>

#include <fbl/alloc_checker.h>
#include <lib/zx/port.h>
#include <soc/as370/as370-dhub-regs.h>
#include <soc/as370/as370-hw.h>
#include <soc/as370/syn-dhub.h>

std::unique_ptr<SynDhub> SynDhub::Create(ddk::MmioBuffer mmio, uint8_t channel_id) {
  fbl::AllocChecker ac;

  if (channel_id >= countof(channel_info_)) {
    return nullptr;
  }

  auto ret = std::unique_ptr<SynDhub>(new (&ac) SynDhub(std::move(mmio), channel_id));
  if (!ac.check()) {
    return nullptr;
  }

  return ret;
}

SynDhub::SynDhub(ddk::MmioBuffer mmio, uint8_t channel_id)
    : mmio_(std::move(mmio)), channel_id_(channel_id) {
  uint32_t fifo_cmd_id = 2 * channel_id_;
  uint32_t fifo_data_id = 2 * channel_id_ + 1;

  // Stop and clear FIFO for cmd and data.
  FiFo_START::Get(fifo_cmd_id).FromValue(0).set_EN(0).WriteTo(&mmio_);
  FiFo_CLEAR::Get(fifo_cmd_id).FromValue(0).set_EN(1).WriteTo(&mmio_);
  FiFo_START::Get(fifo_data_id).FromValue(0).set_EN(0).WriteTo(&mmio_);
  FiFo_CLEAR::Get(fifo_data_id).FromValue(0).set_EN(1).WriteTo(&mmio_);

  // Stop and configure channel.
  channelCtl_START::Get(channel_id_).FromValue(0).WriteTo(&mmio_);
  channelCtl_CFG::Get(channel_id_)
      .FromValue(0)
      .set_selfLoop(0)
      .set_QoS(0)
      .set_MTU(4)  // 128 bytes (2 ^ 4 x 8).
      .WriteTo(&mmio_);
  assert(kMtuSize == 128);

  uint32_t bank = channel_info_[channel_id_].bank;
  uint32_t base_cmd = bank * 512;
  uint32_t base_data = bank * 512 + 32;
  constexpr uint32_t depth_cmd = 4;    // 4 x 8 = 32 bytes.
  constexpr uint32_t depth_data = 60;  // 60 x 8 = 480 bytes, to total 512 bytes.

  // FIFO semaphores use cells with hub == false.

  // FIFO cmd configure and start.
  FiFo_CFG::Get(fifo_cmd_id).FromValue(0).set_BASE(base_cmd).WriteTo(&mmio_);
  cell_CFG::Get(false, fifo_cmd_id).FromValue(0).set_DEPTH(depth_cmd).WriteTo(&mmio_);
  FiFo_START::Get(fifo_cmd_id).FromValue(0).set_EN(1).WriteTo(&mmio_);

  // FIFO data configure and start.
  FiFo_CFG::Get(fifo_data_id).FromValue(0).set_BASE(base_data).WriteTo(&mmio_);
  cell_CFG::Get(false, fifo_data_id).FromValue(0).set_DEPTH(depth_data).WriteTo(&mmio_);
  FiFo_START::Get(fifo_data_id).FromValue(0).set_EN(1).WriteTo(&mmio_);

  // Channel configura and start.
  channelCtl_START::Get(channel_id_).FromValue(0).set_EN(1).WriteTo(&mmio_);
  cell_CFG::Get(true, channel_id_).FromValue(0).set_DEPTH(1).WriteTo(&mmio_);

  auto read = cell_INTR0_mask::Get(true, channel_id_).ReadFrom(&mmio_);
  if (read.reg_value()) {  // Clear interrupt.
    cell_INTR0_mask::Get(true, channel_id_).FromValue(read.reg_value()).WriteTo(&mmio_);
  }
}

void SynDhub::Enable(bool enable) {
  cell_INTR0_mask::Get(true, channel_id_).FromValue(0).set_full(1).WriteTo(&mmio_);

  // Clear the channel.
  uint32_t fifo_cmd_id = 2 * channel_id_;
  uint32_t fifo_data_id = 2 * channel_id_ + 1;
  FiFo_START::Get(fifo_cmd_id).FromValue(0).set_EN(0).WriteTo(&mmio_);        // Stop cmd queue.
  channelCtl_START::Get(channel_id_).FromValue(0).set_EN(0).WriteTo(&mmio_);  // Stop channel.
  channelCtl_CLEAR::Get(channel_id_).FromValue(0).set_EN(1).WriteTo(&mmio_);  // Clear channel.
  while ((BUSY::Get().ReadFrom(&mmio_).ST() | PENDING::Get().ReadFrom(&mmio_).ST()) &
         (1 << channel_id_)) {
  }  // Wait while busy.

  FiFo_START::Get(fifo_cmd_id).FromValue(0).set_EN(0).WriteTo(&mmio_);  // Stop cmd queue.
  FiFo_CLEAR::Get(fifo_cmd_id).FromValue(0).set_EN(1).WriteTo(&mmio_);  // Clear cmd queue.
  while (HBO_BUSY::Get().ReadFrom(&mmio_).ST() & (1 << fifo_cmd_id)) {
  }  // Wait while busy.

  FiFo_START::Get(fifo_data_id).FromValue(0).set_EN(0).WriteTo(&mmio_);  // Stop data queue.
  FiFo_CLEAR::Get(fifo_data_id).FromValue(0).set_EN(1).WriteTo(&mmio_);  // Clear data queue.
  while (HBO_BUSY::Get().ReadFrom(&mmio_).ST() & (1 << fifo_data_id)) {
  }  // Wait while busy.

  if (enable) {
    channelCtl_START::Get(channel_id_).FromValue(0).set_EN(1).WriteTo(&mmio_);  // Start channel.
    FiFo_START::Get(fifo_cmd_id).FromValue(0).set_EN(1).WriteTo(&mmio_);        // Start FIFO.
    FiFo_START::Get(fifo_data_id).FromValue(0).set_EN(1).WriteTo(&mmio_);       // Start FIFO.
  }
}

uint32_t SynDhub::GetBufferPosition() {
  return static_cast<uint32_t>(current_cmd_address_ - dma_address_);
}

void SynDhub::StartDma() {
  uint32_t fifo_cmd_id = 2 * channel_id_;
  constexpr bool producer = false;
  uint16_t ptr = mmio_.Read<uint16_t>(0x1'0500 + (fifo_cmd_id << 2) + (producer << 7) + 2);
  uint32_t base = (channel_info_[channel_id_].bank * 2) << 8;
  uint32_t size = channel_info_[channel_id_].size / kMtuSize;
  // Write to SRAM.
  CommandAddress::Get(base + ptr * 8)
      .FromValue(0)
      .set_addr(static_cast<uint32_t>(current_cmd_address_))
      .WriteTo(&mmio_);
  CommandHeader::Get(base + ptr * 8)
      .FromValue(0)
      .set_interrupt(1)
      .set_sizeMTU(1)
      .set_size(size)
      .WriteTo(&mmio_);
  PUSH::Get(false).FromValue(0).set_ID(fifo_cmd_id).set_delta(1).WriteTo(&mmio_);
}

void SynDhub::Ack() {
  auto interrupt_status = full::Get(true).ReadFrom(&mmio_).reg_value();
  if (!(interrupt_status & (1 << channel_id_))) {
    return;
  }
  POP::Get(true).FromValue(0).set_delta(1).set_ID(channel_id_).WriteTo(&mmio_);
  full::Get(true).ReadFrom(&mmio_).set_ST(1 << channel_id_).WriteTo(&mmio_);
  current_cmd_address_ += channel_info_[channel_id_].size;
  while (current_cmd_address_ >= dma_address_ + dma_size_) {
    current_cmd_address_ = dma_address_;
  }
}

void SynDhub::SetBuffer(zx_paddr_t buf, size_t len) {
  dma_address_ = buf;
  dma_size_ = len;
  current_cmd_address_ = dma_address_;
}
