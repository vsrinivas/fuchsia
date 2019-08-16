// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/clock.h>
#include <lib/zx/vmar.h>
#include <unistd.h>

#include <limits>
#include <utility>

#include <fbl/alloc_checker.h>
#include <soc/as370/as370-audio-regs.h>
#include <soc/as370/as370-clk-regs.h>
#include <soc/as370/as370-dma.h>
#include <soc/as370/syn-audio-in.h>

namespace {
constexpr uint64_t kPortDmaNotification = 0x00;
}  // namespace

std::unique_ptr<SynAudioInDevice> SynAudioInDevice::Create(ddk::MmioBuffer mmio_global,
                                                           ddk::MmioBuffer mmio_avio_global,
                                                           ddk::MmioBuffer mmio_i2s,
                                                           ddk::SharedDmaProtocolClient dma) {
  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<SynAudioInDevice>(new (&ac) SynAudioInDevice(
      std::move(mmio_global), std::move(mmio_avio_global), std::move(mmio_i2s), dma));
  if (!ac.check()) {
    return nullptr;
  }

  auto status = dev->Init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not init %d\n", __FILE__, status);
    return nullptr;
  }
  return dev;
}

SynAudioInDevice::SynAudioInDevice(ddk::MmioBuffer mmio_global, ddk::MmioBuffer mmio_avio_global,
                                   ddk::MmioBuffer mmio_i2s, ddk::SharedDmaProtocolClient dma)
    : global_(std::move(mmio_global)),
      avio_global_(std::move(mmio_avio_global)),
      i2s_(std::move(mmio_i2s)),
      dma_(dma) {
  cic_filter_ = std::make_unique<CicFilter>();
}

uint32_t SynAudioInDevice::fifo_depth() const { return dma_.GetTransferSize(DmaId::kDmaIdPdmW0); }

void SynAudioInDevice::ProcessDma() {
  const uint32_t dma_transfer_size = dma_.GetTransferSize(DmaId::kDmaIdPdmW0);
  while (1) {
    static uint32_t run_count = 0;
    auto before = zx::clock::get_monotonic();
    auto dhub_pos = dma_.GetBufferPosition(DmaId::kDmaIdPdmW0);
    auto amount_pdm = dhub_pos - dma_buffer_current_;
    auto distance = dma_buffer_size_ - amount_pdm;

    // Check for usual case, wrap around, or no work to do.
    if (dhub_pos > dma_buffer_current_) {
      zxlogf(TRACE, "audio: usual  run %u  distance 0x%08X  dhub 0x%08X  curr 0x%08X  pdm 0x%08X\n",
             run_count, distance, dhub_pos, dma_buffer_current_, amount_pdm);
    } else if (dhub_pos < dma_buffer_current_) {
      distance = dma_buffer_current_ - dhub_pos;
      amount_pdm = dma_buffer_size_ - distance;
      zxlogf(TRACE, "audio: wrap   run %u  distance 0x%08X  dhub 0x%08X  curr 0x%08X  pdm 0x%08X\n",
             run_count, distance, dhub_pos, dma_buffer_current_, amount_pdm);
    } else {
      zxlogf(TRACE, "audio: empty  run %u  distance 0x%08X  dhub 0x%08X  curr 0x%08X  pdm 0x%08X\n",
             run_count, distance, dhub_pos, dma_buffer_current_, amount_pdm);
      return;
    }

    run_count++;

    // Check for overflowing.
    if (distance <= dma_transfer_size) {
      overflows_++;
      zxlogf(ERROR, "audio: overflows %u\n", overflows_);
      return;  // We can't keep up.
    }

    const uint32_t max_dma_to_process = dma_transfer_size;
    if (amount_pdm > max_dma_to_process) {
      zxlogf(TRACE, "audio: PDM data (%u) from dhub is too big (>%u),  overflows %u\n", amount_pdm,
             max_dma_to_process, overflows_);
      amount_pdm = max_dma_to_process;
    }

    // Decode. TODO(andresoportus): Decode the other (third) michrophone on PDM1.
    constexpr uint32_t multiplier_shift = 5;
    auto amount_pcm0 =
        cic_filter_->Filter(0, reinterpret_cast<void*>(dma_base_ + dma_buffer_current_), amount_pdm,
                            reinterpret_cast<void*>(ring_buffer_base_ + ring_buffer_current_), 2, 0,
                            2, 0, multiplier_shift);
    auto amount_pcm1 =
        cic_filter_->Filter(1, reinterpret_cast<void*>(dma_base_ + dma_buffer_current_), amount_pdm,
                            reinterpret_cast<void*>(ring_buffer_base_ + ring_buffer_current_), 2, 1,
                            2, 1, multiplier_shift);
    if (amount_pcm0 != amount_pcm1) {
      zxlogf(ERROR, "audio: different amounts for PCM decoding %u %u\n", amount_pcm0, amount_pcm1);
    }

    // Increment output (ring buffer) pointer and check for wraparound.
    ring_buffer_current_ += amount_pcm0;
    if (ring_buffer_current_ >= ring_buffer_size_) {
      ring_buffer_current_ = 0;
    }

    // Increment input (DMA buffer) pointer and check for wraparound.
    dma_buffer_current_ += amount_pdm;
    if (dma_buffer_current_ >= dma_buffer_size_) {
      dma_buffer_current_ -= dma_buffer_size_;
    }

    // Clean cache for the next input (DMA buffer).
    auto buffer_to_clean = max_dma_to_process;
    ZX_ASSERT(dma_buffer_current_ + buffer_to_clean <= dma_buffer_size_);
    dma_buffer_.op_range(ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, dma_buffer_current_, buffer_to_clean,
                         nullptr, 0);

    auto after = zx::clock::get_monotonic();
    zxlogf(TRACE, "audio: decoded 0x%X bytes in %lumsecs  distance 0x%X\n", amount_pdm,
           (after - before).to_msecs(), distance);
  }
}

int SynAudioInDevice::Thread() {
  while (1) {
    zx_port_packet_t packet;
    auto status = port_.wait(zx::time::infinite(), &packet);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s port wait failed: %d\n", __FILE__, status);
      return thrd_error;
    }
    zxlogf(TRACE, "audio: msg on port key %lu\n", packet.key);
    if (packet.key == kPortDmaNotification) {
      if (enabled_) {
        ProcessDma();
      } else {
        zxlogf(TRACE, "audio: DMA already stopped\n");
      }
    }
  }
}

zx_status_t SynAudioInDevice::Init() {
  auto status = zx::port::create(0, &port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s port create failed %d\n", __FILE__, status);
    return status;
  }

  dma_notify_t notify = {};
  auto notify_cb = [](void* ctx, dma_state_t state) -> void {
    SynAudioInDevice* thiz = static_cast<SynAudioInDevice*>(ctx);
    zx_port_packet packet = {kPortDmaNotification, ZX_PKT_TYPE_USER, ZX_OK, {}};
    zxlogf(TRACE, "dhub: notification callback with state %d\n", static_cast<int>(state));
    // No need to notify if we already stopped the DMA.
    if (thiz->enabled_) {
      auto status = thiz->port_.queue(&packet);
      ZX_ASSERT(status == ZX_OK);
    }
  };
  notify.callback = notify_cb;
  notify.ctx = this;
  dma_.SetNotifyCallback(DmaId::kDmaIdPdmW0, &notify);

  auto cb = [](void* arg) -> int { return reinterpret_cast<SynAudioInDevice*>(arg)->Thread(); };
  int rc = thrd_create_with_name(&thread_, cb, this, "synaptics-audio-in-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

uint32_t SynAudioInDevice::GetRingPosition() { return ring_buffer_current_; }

zx_status_t SynAudioInDevice::GetBuffer(size_t size, zx::vmo* buffer) {
  // dma_buffer_size (ask here is a buffer of size 8 x 16KB) allows for this driver not
  // getting CPU time to perform the PDM decoding.  Higher numbers allow for more resiliance,
  // although if we get behind on decoding there is more latency added to the created ringbuffer.
  dma_.InitializeAndGetBuffer(DmaId::kDmaIdPdmW0, DMA_TYPE_CYCLIC, 8 * 16 * 1024, &dma_buffer_);
  size_t buffer_size = 0;
  dma_buffer_.get_size(&buffer_size);
  dma_buffer_size_ = static_cast<uint32_t>(buffer_size);

  auto root = zx::vmar::root_self();
  constexpr uint32_t flags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  auto status = root->map(0, dma_buffer_, 0, dma_buffer_size_, flags, &dma_base_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s vmar mapping failed %d\n", __FILE__, status);
    return status;
  }
  dma_buffer_.op_range(ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, 0, dma_buffer_size_, nullptr, 0);

  status = zx::vmo::create(size, 0, &ring_buffer_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to allocate ring buffer vmo %d\n", __FILE__, status);
    return status;
  }
  ring_buffer_.get_size(&buffer_size);
  ring_buffer_size_ = static_cast<uint32_t>(buffer_size);
  status = root->map(0, ring_buffer_, 0, ring_buffer_size_, flags, &ring_buffer_base_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s vmar mapping failed %d\n", __FILE__, status);
    return status;
  }
  constexpr uint32_t rights =
      ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE;
  return ring_buffer_.duplicate(rights, buffer);
}

uint64_t SynAudioInDevice::Start() {
  AIO_IRQENABLE::Get().ReadFrom(&i2s_).set_PDMIRQ(1).WriteTo(&i2s_);
  AIO_MCLKPDM_ACLK_CTRL::Get().FromValue(0x189).WriteTo(&i2s_);
  constexpr uint32_t divider = 3;  // divide by 8.
  AIO_PDM_CTRL1::Get()
      .FromValue(0)
      .set_RDM(4)
      .set_RSLB(1)
      .set_INVCLK_INT(1)
      .set_CLKDIV(divider)
      .WriteTo(&i2s_);

  AIO_PDM_PDM0_CTRL::Get().FromValue(0).set_MUTE(1).set_ENABLE(0).WriteTo(&i2s_);
  AIO_PDM_PDM1_CTRL::Get().FromValue(0).set_MUTE(1).set_ENABLE(0).WriteTo(&i2s_);

  AIO_PDM_PDM0_CTRL::Get().FromValue(0).set_MUTE(1).set_ENABLE(1).WriteTo(&i2s_);
  AIO_PDM_PDM1_CTRL::Get().FromValue(0).set_MUTE(1).set_ENABLE(1).WriteTo(&i2s_);

  AIO_PDM_MIC_SEL::Get().FromValue(0).set_CTRL(0x4).WriteTo(&i2s_);
  AIO_PDM_MIC_SEL::Get().FromValue(0).set_CTRL(0xc).WriteTo(&i2s_);

  AIO_PDM_PDM0_CTRL2::Get().FromValue(0).set_FDLT(3).set_RDLT(3).WriteTo(&i2s_);
  AIO_PDM_PDM1_CTRL2::Get().FromValue(0).set_FDLT(3).set_RDLT(3).WriteTo(&i2s_);

  // Playback.
  enabled_ = true;
  dma_.Start(DmaId::kDmaIdPdmW0);

  // Unmute.
  AIO_PDM_PDM0_CTRL::Get().FromValue(0).set_MUTE(0).set_ENABLE(1).WriteTo(&i2s_);
  AIO_PDM_PDM1_CTRL::Get().FromValue(0).set_MUTE(0).set_ENABLE(1).WriteTo(&i2s_);

  // Enable.
  AIO_IOSEL_PDM::Get().FromValue(0).set_GENABLE(1).WriteTo(&i2s_);
  return 0;
}

void SynAudioInDevice::Stop() {
  AIO_IOSEL_PDM::Get().FromValue(0).set_GENABLE(0).WriteTo(&i2s_);
  enabled_ = false;
  dma_.Stop(DmaId::kDmaIdPdmW0);
}

void SynAudioInDevice::Shutdown() { Stop(); }
