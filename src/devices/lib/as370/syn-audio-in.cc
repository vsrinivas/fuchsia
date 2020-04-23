// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/clock.h>
#include <lib/zx/vmar.h>
#include <unistd.h>

#include <limits>
#include <optional>
#include <utility>

#include <fbl/algorithm.h>
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
    zxlogf(ERROR, "%s could not init %d", __FILE__, status);
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

uint32_t SynAudioInDevice::PcmAmountPerTransfer() const {
  constexpr uint32_t kChannelsPerDma = 2;
  const uint32_t kTransferSize = dma_.GetTransferSize(DmaId::kDmaIdPdmW0);
  ZX_DEBUG_ASSERT(kTransferSize % (kChannelsPerDma * cic_filter_->GetInputToOutputRatio()) == 0);
  const uint32_t kPcmDataForOneChannel =
      kTransferSize / (kChannelsPerDma * cic_filter_->GetInputToOutputRatio());
  return static_cast<uint32_t>(kNumberOfChannels) * kPcmDataForOneChannel;
}

uint32_t SynAudioInDevice::FifoDepth() const {
  constexpr uint32_t kNumberOfTransfersForFifoDepth = 2;
  return kNumberOfTransfersForFifoDepth * PcmAmountPerTransfer();
}

void SynAudioInDevice::ProcessDma(uint32_t index) {
  const uint32_t dma_transfer_size = dma_.GetTransferSize(DmaId::kDmaIdPdmW0);
  while (1) {
    static uint32_t run_count = 0;
    auto before = zx::clock::get_monotonic();
    auto dhub_pos = dma_.GetBufferPosition(index == 0 ? DmaId::kDmaIdPdmW0 : DmaId::kDmaIdPdmW1);
    auto amount_pdm = dhub_pos - dma_buffer_current_[index];
    auto distance = dma_buffer_size_[index] - amount_pdm;

    // Check for usual case, wrap around, or no work to do.
    if (dhub_pos > dma_buffer_current_[index]) {
      zxlogf(TRACE,
             "audio: %u  usual  run %u  distance 0x%08X  dhub 0x%08X  curr 0x%08X  pdm 0x%08X\n",
             index, run_count, distance, dhub_pos, dma_buffer_current_[index], amount_pdm);
    } else if (dhub_pos < dma_buffer_current_[index]) {
      distance = dma_buffer_current_[index] - dhub_pos;
      amount_pdm = dma_buffer_size_[index] - distance;
      zxlogf(TRACE,
             "audio: %u  wrap   run %u  distance 0x%08X  dhub 0x%08X  curr 0x%08X  pdm 0x%08X\n",
             index, run_count, distance, dhub_pos, dma_buffer_current_[index], amount_pdm);
    } else {
      zxlogf(TRACE,
             "audio: %u  empty  run %u  distance 0x%08X  dhub 0x%08X  curr 0x%08X  pdm 0x%08X\n",
             index, run_count, distance, dhub_pos, dma_buffer_current_[index], amount_pdm);
      return;
    }

    run_count++;

    // Check for overflowing.
    if (distance <= dma_transfer_size) {
      overflows_++;
      zxlogf(ERROR, "audio: %u  overflows %u", index, overflows_);
      return;  // We can't keep up.
    }

    const uint32_t max_dma_to_process = dma_transfer_size;
    if (amount_pdm > max_dma_to_process) {
      zxlogf(TRACE, "audio: %u  PDM data (%u) from dhub is too big (>%u),  overflows %u", index,
             amount_pdm, max_dma_to_process, overflows_);
      amount_pdm = max_dma_to_process;
    }

    constexpr uint32_t multiplier_shift = 5;
    struct Parameter {
      uint32_t filter_index;
      uint32_t input_channel;
      uint32_t output_channel;
    };

    std::optional<Parameter> parameters[][2] = {
        {std::optional<Parameter>({0, 0, 0}), std::optional<Parameter>({1, 1, 1})},  // index 0.
        {std::optional<Parameter>({2, 0, 2}), std::optional<Parameter>()},           // index 1.
    };
    uint32_t amount_pcm = 0;
    // Either input channel (rising or falled edge PDM capture), unless it is only one channel.
    for (uint32_t i = 0; i < (kNumberOfChannels > 1 ? 2 : 1); ++i) {
      if (parameters[index][i].has_value()) {
        zxlogf(TRACE, "audio: %u  decoding from 0x%08X  amount 0x%08X  into 0x%08X", index,
               dma_buffer_current_[index], amount_pdm, ring_buffer_current_);
        amount_pcm = cic_filter_->Filter(
            parameters[index][i]->filter_index,
            reinterpret_cast<void*>(dma_base_[index] + dma_buffer_current_[index]), amount_pdm,
            reinterpret_cast<void*>(ring_buffer_base_ + ring_buffer_current_), 2,
            parameters[index][i]->input_channel, kNumberOfChannels,
            parameters[index][i]->output_channel, multiplier_shift);
      }
    }

    // Increment output (ring buffer) pointer and check for wraparound on the last DMA.
    if (index == kNumberOfDmas - 1) {
      ring_buffer_current_ += amount_pcm;
      if (ring_buffer_current_ >= ring_buffer_size_) {
        ring_buffer_current_ = 0;
      }
    }

    // Increment input (DMA buffer) pointer and check for wraparound.
    dma_buffer_current_[index] += amount_pdm;
    if (dma_buffer_current_[index] >= dma_buffer_size_[index]) {
      dma_buffer_current_[index] -= dma_buffer_size_[index];
    }

    // Clean cache for the next input (DMA buffer).
    auto buffer_to_clean = max_dma_to_process;
    ZX_ASSERT(dma_buffer_current_[index] + buffer_to_clean <= dma_buffer_size_[index]);
    dma_buffer_[index].op_range(ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, dma_buffer_current_[index],
                                buffer_to_clean, nullptr, 0);
    auto after = zx::clock::get_monotonic();
    zxlogf(TRACE, "audio: %u  decoded 0x%X bytes in %lumsecs  into 0x%X bytes  distance 0x%X",
           index, amount_pdm, (after - before).to_msecs(), amount_pcm, distance);
  }
}

int SynAudioInDevice::Thread() {
  while (1) {
    zx_port_packet_t packet;
    auto status = port_.wait(zx::time::infinite(), &packet);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s port wait failed: %d", __FILE__, status);
      return thrd_error;
    }
    zxlogf(TRACE, "audio: msg on port key %lu", packet.key);
    if (packet.key == kPortDmaNotification) {
      if (enabled_) {
        for (uint32_t i = 0; i < kNumberOfDmas; ++i) {
          ProcessDma(i);
        }
      } else {
        zxlogf(TRACE, "audio: DMA already stopped");
      }
    }
  }
}

zx_status_t SynAudioInDevice::Init() {
  auto status = zx::port::create(0, &port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s port create failed %d", __FILE__, status);
    return status;
  }

  dma_notify_t notify = {};
  auto notify_cb = [](void* ctx, dma_state_t state) -> void {
    SynAudioInDevice* thiz = static_cast<SynAudioInDevice*>(ctx);
    zx_port_packet packet = {kPortDmaNotification, ZX_PKT_TYPE_USER, ZX_OK, {}};
    zxlogf(TRACE, "audio: notification callback with state %d", static_cast<int>(state));
    // No need to notify if we already stopped the DMA.
    if (thiz->enabled_) {
      auto status = thiz->port_.queue(&packet);
      ZX_ASSERT(status == ZX_OK);
    }
  };
  notify.callback = notify_cb;
  notify.ctx = this;
  dma_.SetNotifyCallback(DmaId::kDmaIdPdmW0, &notify);
  // Only need notification for PDM0, PDM1 piggybacks onto it.

  auto cb = [](void* arg) -> int { return reinterpret_cast<SynAudioInDevice*>(arg)->Thread(); };
  int rc = thrd_create_with_name(&thread_, cb, this, "synaptics-audio-in-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

uint32_t SynAudioInDevice::GetRingPosition() { return ring_buffer_current_; }

zx_status_t SynAudioInDevice::GetBuffer(size_t size, zx::vmo* buffer) {
  // dma_buffer_size (ask here is a buffer of size 8 x 16KB) allows for this driver not getting CPU
  // time to perform the PDM decoding even when behind.  Higher numbers allow for more resiliance,
  // although if we get behind on decoding there is more latency added to the created ringbuffer.
  // Note though that it is expected for the driver to decode one transfer within the time it takes
  // to receive the next as reported on fifo_depth() (kNumberOfTransfersForFifoDepth == 2).
  ZX_ASSERT(dma_.GetTransferSize(DmaId::kDmaIdPdmW0) == dma_.GetTransferSize(DmaId::kDmaIdPdmW1));
  ZX_ASSERT(kNumberOfDmas <= 2);

  auto root = zx::vmar::root_self();
  size_t buffer_size = 0;
  for (uint32_t i = 0; i < kNumberOfDmas; ++i) {
    dma_.InitializeAndGetBuffer(i == 0 ? DmaId::kDmaIdPdmW0 : DmaId::kDmaIdPdmW1, DMA_TYPE_CYCLIC,
                                8 * 16 * 1024, &dma_buffer_[i]);
    dma_buffer_[i].get_size(&buffer_size);
    dma_buffer_size_[i] = static_cast<uint32_t>(buffer_size);

    constexpr uint32_t flags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
    auto status = root->map(0, dma_buffer_[i], 0, dma_buffer_size_[i], flags, &dma_base_[i]);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s vmar mapping failed %d", __FILE__, status);
      return status;
    }
    dma_buffer_[i].op_range(ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, 0, dma_buffer_size_[i], nullptr, 0);
  }

  // We simplify buffer management by having decoded PCM data for all channels to not wrap at the
  // end of ring buffer, rounding up to the decoded PCM data amount per transfer.
  size = fbl::round_up<size_t, uint32_t>(size, PcmAmountPerTransfer());

  auto status = zx::vmo::create(size, 0, &ring_buffer_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to allocate ring buffer vmo %d", __FILE__, status);
    return status;
  }
  ring_buffer_.get_size(&buffer_size);
  ring_buffer_size_ = static_cast<uint32_t>(buffer_size);
  constexpr uint32_t flags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  status = root->map(0, ring_buffer_, 0, ring_buffer_size_, flags, &ring_buffer_base_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s vmar mapping failed %d", __FILE__, status);
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
  for (uint32_t i = 0; i < kNumberOfDmas; ++i) {
    dma_.Start(i == 0 ? DmaId::kDmaIdPdmW0 : DmaId::kDmaIdPdmW1);
  }

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
  for (uint32_t i = 0; i < kNumberOfDmas; ++i) {
    dma_.Stop(i == 0 ? DmaId::kDmaIdPdmW0 : DmaId::kDmaIdPdmW1);
  }
}

void SynAudioInDevice::Shutdown() { Stop(); }
