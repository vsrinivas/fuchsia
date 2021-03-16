// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SHAREDDMA_DRIVERS_SYN_DMA_SYN_DHUB_H_
#define SRC_DEVICES_SHAREDDMA_DRIVERS_SYN_DMA_SYN_DHUB_H_
#include <assert.h>
#include <fuchsia/hardware/shareddma/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/port.h>
#include <threads.h>
#include <zircon/types.h>

#include <memory>
#include <utility>

#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <soc/as370/as370-dma.h>

namespace as370 {

class SynDhub;
using DeviceType = ddk::Device<SynDhub, ddk::Unbindable>;

class SynDhub : public DeviceType, public ddk::SharedDmaProtocol<SynDhub, ddk::base_protocol> {
 public:
  // Move operators are implicitly disabled.
  SynDhub(const SynDhub&) = delete;
  SynDhub& operator=(const SynDhub&) = delete;

  static std::unique_ptr<SynDhub> Create(zx_device_t* parent);

  // Shared DMA protocol.
  zx_status_t SharedDmaInitializeAndGetBuffer(uint32_t dma_id, dma_type_t type, uint32_t t_len,
                                              zx::vmo* out_vmo);
  void SharedDmaStart(uint32_t dma_id) { Enable(dma_id, true); }
  void SharedDmaStop(uint32_t dma_id) { Enable(dma_id, false); }
  uint32_t SharedDmaGetTransferSize(uint32_t dma_id);
  uint32_t SharedDmaGetBufferPosition(uint32_t dma_id);
  zx_status_t SharedDmaSetNotifyCallback(uint32_t dma_id, const dma_notify_t* cb);

  void DdkUnbind(ddk::UnbindTxn txn) {
    Shutdown();
    txn.Reply();
  }
  void DdkRelease() { delete this; }

 protected:
  SynDhub(zx_device_t* device, ddk::MmioBuffer mmio) : DeviceType(device), mmio_(std::move(mmio)) {}
  void StartDma(uint32_t dma_id, bool trigger_interrupt);       // protected for unit tests.
  void SetBuffer(uint32_t dma_id, zx_paddr_t buf, size_t len);  // protected for unit tests.
  void Init(uint32_t dma_id);                                   // protected for unit tests.
  void Enable(uint32_t channel_id, bool enable);                // protected for unit tests.

 private:
  static constexpr uint32_t kMtuSize = 128;
  static constexpr uint32_t kConcurrentDmas = 1;
  static constexpr struct {
    uint32_t bank;
    uint32_t fifo_data_depth;
    uint32_t dma_mtus;  // We use 64 for I2S and 128 for PDM.
  } channel_info_[] = {
      // clang-format off
    { 0,  60,  64},
    { 1,  60,  64},
    { 2,  60,  64},
    { 3,  60,  64},
    { 4,  60,  64},
    { 5,  60,  64},
    { 6,  60, 128},
    { 7, 252,  64},
    {11,  48,  64},
    {12,  60, 128},
    {13,  60, 128},
    {14,  60, 128},
    {15,  60,  64},
    {16,  60,  64},
    { 8, 252,  64},
    {10, 252,  64},
      // clang-format on
  };

  zx_status_t Bind();
  int Thread();
  void Shutdown();
  void Ack(uint32_t channel_id);
  void ProcessIrq(uint32_t channel_id);

  ddk::MmioBuffer mmio_;
  zx::port port_;
  zx::interrupt interrupt_;
  thrd_t thread_;
  zx::bti bti_;
  fbl::Mutex position_lock_;
  // clang-format off
  bool enabled_                    [DmaId::kDmaIdMax] = {};
  dma_notify_t callback_           [DmaId::kDmaIdMax] = {};
  fzl::PinnedVmo pinned_dma_buffer_[DmaId::kDmaIdMax] = {};
  zx::vmo    dma_buffer_           [DmaId::kDmaIdMax] = {};
  uint32_t   dma_size_             [DmaId::kDmaIdMax] = {};
  zx_paddr_t dma_base_             [DmaId::kDmaIdMax] = {};
  zx_paddr_t dma_current_          [DmaId::kDmaIdMax] TA_GUARDED(position_lock_);
  dma_type_t type_                 [DmaId::kDmaIdMax] = {};
  bool       triggers_interrupt_   [DmaId::kDmaIdMax] = {};
  // clang-format on
};

}  // namespace as370

#endif  // SRC_DEVICES_SHAREDDMA_DRIVERS_SYN_DMA_SYN_DHUB_H_
