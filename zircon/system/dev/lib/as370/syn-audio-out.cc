// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <utility>

#include <fbl/alloc_checker.h>
#include <soc/as370/as370-audio-regs.h>
#include <soc/as370/as370-clk-regs.h>
#include <soc/as370/syn-audio-out.h>

namespace {
constexpr uint64_t kPortKeyIrqMsg = 0x00;
constexpr uint64_t kPortShutdown = 0x01;
}  // namespace

std::unique_ptr<SynAudioOutDevice> SynAudioOutDevice::Create(ddk::MmioBuffer mmio_global,
                                                             ddk::MmioBuffer mmio_dhub,
                                                             ddk::MmioBuffer mmio_avio_global,
                                                             ddk::MmioBuffer mmio_i2s,
                                                             zx::interrupt interrupt) {
  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<SynAudioOutDevice>(
      new (&ac) SynAudioOutDevice(std::move(mmio_global), std::move(mmio_avio_global),
                                  std::move(mmio_i2s), std::move(interrupt)));
  if (!ac.check()) {
    return nullptr;
  }

  auto status = dev->Init(std::move(mmio_dhub));
  if (status != ZX_OK) {
    return nullptr;
  }
  return dev;
}

SynAudioOutDevice::SynAudioOutDevice(ddk::MmioBuffer mmio_global, ddk::MmioBuffer mmio_avio_global,
                                     ddk::MmioBuffer mmio_i2s, zx::interrupt interrupt)
    : global_(std::move(mmio_global)),
      avio_global_(std::move(mmio_avio_global)),
      i2s_(std::move(mmio_i2s)),
      interrupt_(std::move(interrupt)) {}

int SynAudioOutDevice::Thread() {
  while (1) {
    zx_port_packet_t packet = {};
    auto status = port_.wait(zx::time::infinite(), &packet);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s port wait failed: %d\n", __func__, status);
      return thrd_error;
    }
    zxlogf(TRACE, "%s msg on port key %lu\n", __func__, packet.key);
    if (packet.key == kPortShutdown) {
      zxlogf(INFO, "audio: Synaptics audio out shutting down\n");
      return thrd_success;
    } else if (packet.key == kPortKeyIrqMsg) {
      dhub_->Ack();
      if (enabled_) {
        dhub_->StartDma();
      }
      interrupt_.ack();
    }
  }
}

zx_status_t SynAudioOutDevice::Init(ddk::MmioBuffer mmio_dhub) {
  dhub_ = SynDhub::Create(std::move(mmio_dhub), SynDhub::kChannelIdOut);
  if (dhub_ == nullptr) {
    return ZX_ERR_INTERNAL;
  }

  auto status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s port create failed %d\n", __func__, status);
    return status;
  }

  status = interrupt_.bind(port_, kPortKeyIrqMsg, 0 /*options*/);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s interrupt bind failed %d\n", __func__, status);
    return status;
  }

  auto cb = [](void* arg) -> int { return reinterpret_cast<SynAudioOutDevice*>(arg)->Thread(); };
  int rc = thrd_create_with_name(&thread_, cb, this, "synaptics-audio-out-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  AIO_PRI_TSD0_PRI_CTRL::Get().ReadFrom(&i2s_).set_ENABLE(0).WriteTo(&i2s_);  // Disable channel 0.
  AIO_IRQENABLE::Get().ReadFrom(&i2s_).set_PRIIRQ(1).WriteTo(&i2s_);
  AIO_PRI_PRIPORT::Get().ReadFrom(&i2s_).set_ENABLE(1).WriteTo(&i2s_);

  return ZX_OK;
}

uint32_t SynAudioOutDevice::GetRingPosition() { return dhub_->GetBufferPosition(); }

zx_status_t SynAudioOutDevice::SetBuffer(zx_paddr_t buf, size_t len) {
  constexpr uint32_t kDmaMinAlignement = 16;
  if ((buf % kDmaMinAlignement) || ((buf + len - 1) > std::numeric_limits<uint32_t>::max()) ||
      (len < GetDmaGranularity()) || (len % GetDmaGranularity())) {
    return ZX_ERR_INVALID_ARGS;
  }
  dhub_->SetBuffer(buf, len);
  return ZX_OK;
}

uint64_t SynAudioOutDevice::Start() {
  AIO_PRI_TSD0_PRI_CTRL::Get().FromValue(0).set_ENABLE(1).set_MUTE(1).WriteTo(&i2s_);

  constexpr uint32_t divider = 4;  // BCLK = MCLK (24.576 MHz) / 8 = 3.072 MHz.
  AIO_PRI_PRIAUD_CLKDIV::Get().FromValue(0).set_SETTING(divider).WriteTo(&i2s_);

  AIO_MCLKPRI_ACLK_CTRL::Get()
      .FromValue(0)
      .set_sw_sync_rst(1)
      .set_clkSel(4)  // MCLK = APLL0 (196.608MHz) / 8 = 24.576MHz.
      .set_clkSwitch(1)
      .set_clk_Enable(1)
      .WriteTo(&i2s_);

  // Set I2S, 48K, 32 bits.  So BCLK must be 32 * 2 * 48K = 3.072MHz.
  AIO_PRI_PRIAUD_CTRL::Get()
      .FromValue(0)
      .set_LEFTJFY(0)  // left.
      .set_INVCLK(0)
      .set_INVFS(0)
      .set_TLSB(0)     // MSB first.
      .set_TDM(0)      // Channel resolution, 16 bits per channel.
      .set_TCF(2)      // 32 bit-cloks for FSYNC half-period.
      .set_TFM(2)      // I2S.
      .set_TDMMODE(0)  // I2S.
      .set_TDMWSHIGH(0)
      .WriteTo(&i2s_);

  enabled_ = true;
  dhub_->Enable(true);
  dhub_->StartDma();

  AIO_PRI_TSD0_PRI_CTRL::Get().FromValue(0).set_ENABLE(1).set_MUTE(0).WriteTo(&i2s_);
  return 0;
}

void SynAudioOutDevice::Stop() {
  AIO_PRI_TSD0_PRI_CTRL::Get().ReadFrom(&i2s_).set_MUTE(1).WriteTo(&i2s_);
  enabled_ = false;
}

void SynAudioOutDevice::Shutdown() {
  Stop();
  AIO_PRI_PRIPORT::Get().ReadFrom(&i2s_).set_ENABLE(0).WriteTo(&i2s_);
  zx_port_packet packet = {kPortShutdown, ZX_PKT_TYPE_USER, ZX_OK, {}};
  zx_status_t status = port_.queue(&packet);
  ZX_ASSERT(status == ZX_OK);
  thrd_join(thread_, NULL);
  interrupt_.destroy();
}
