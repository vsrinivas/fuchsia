// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_LIB_AS370_INCLUDE_SOC_AS370_SYN_DHUB_H_
#define ZIRCON_SYSTEM_DEV_LIB_AS370_INCLUDE_SOC_AS370_SYN_DHUB_H_

#include <assert.h>

#include <memory>
#include <utility>

#include <lib/mmio/mmio.h>

class SynDhub {
 public:
  static constexpr uint32_t kChannelIdOut = 0;

  // Move operators are implicitly disabled.
  SynDhub(const SynDhub&) = delete;
  SynDhub& operator=(const SynDhub&) = delete;

  static std::unique_ptr<SynDhub> Create(ddk::MmioBuffer mmio, uint8_t channel);

  void SetBuffer(zx_paddr_t buf, size_t len);
  void Enable(bool enable);
  void StartDma();
  void Ack();
  uint32_t GetBufferPosition();

 protected:
  SynDhub(ddk::MmioBuffer mmio, uint8_t channel);  // protected for unit tests.

 private:
  static constexpr uint32_t kMtuSize = 128;
  static constexpr struct {
    uint32_t bank;
    uint32_t size;
  } channel_info_[] = {
      {0, 0x40 * kMtuSize},
  };

  ddk::MmioBuffer mmio_;
  uint8_t channel_id_;
  zx_paddr_t dma_address_ = 0;
  zx_paddr_t current_cmd_address_ = 0;
  size_t dma_size_ = 0;
};

#endif  // ZIRCON_SYSTEM_DEV_LIB_AS370_INCLUDE_SOC_AS370_SYN_DHUB_H_
