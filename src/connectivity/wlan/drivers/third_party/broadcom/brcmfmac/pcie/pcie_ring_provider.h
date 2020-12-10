// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_RING_PROVIDER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_RING_PROVIDER_H_

#include <zircon/types.h>

#include <atomic>
#include <memory>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_interfaces.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_buscore.h"

namespace wlan {
namespace brcmfmac {

class DmaBuffer;
class PcieFirmware;
class ReadDmaRing;
class WriteDmaRing;

// This class manages the DMA rings used to submit to and receive completions from the brcmfmac
// chipset.  It also provides some DMA-related configuration information to users, through the
// DmaConfig informational struct.
class PcieRingProvider : public DmaRingProviderInterface {
 public:
  // DMA ring configuration information.
  struct DmaRingConfig;

  PcieRingProvider();
  ~PcieRingProvider() override;

  // Static factory function for PcieRingProvider instances.
  static zx_status_t Create(PcieBuscore* buscore, PcieFirmware* firmware,
                            std::unique_ptr<PcieRingProvider>* out_ring_provider);

  // DmaRingProviderInterface implementation.
  const DmaConfig& GetDmaConfig() const override;
  WriteDmaRing* GetControlSubmitRing() override;
  WriteDmaRing* GetRxBufferSubmitRing() override;
  ReadDmaRing* GetControlCompleteRing() override;
  ReadDmaRing* GetTxCompleteRing() override;
  ReadDmaRing* GetRxCompleteRing() override;
  zx_status_t CreateFlowRing(int flow_ring_index,
                             std::unique_ptr<WriteDmaRing>* out_flow_ring) override;

 private:
  // Utility functions for creating DMA rings.
  zx_status_t CreateReadDmaRing(const DmaRingConfig& config,
                                std::unique_ptr<ReadDmaRing>* out_ring);
  zx_status_t CreateWriteDmaRing(const DmaRingConfig& config,
                                 std::unique_ptr<WriteDmaRing>* out_ring);

  PcieBuscore* buscore_ = nullptr;
  DmaConfig dma_config_ = {};
  std::unique_ptr<DmaBuffer> index_buffer_;

  // PCIE core window.  We hold on to ownership of this instance throughout our lifetime, as we
  // don't wan't to be switching away the BAR0 window while servicing interrupts.
  std::unique_ptr<PcieBuscore::PcieRegisterWindow> pcie_core_window_;

  // DMA ring state.  These are arrays of DMA ring index pointers, existing in coherent RAM shared
  // with the firmware.  They are only valid as long as `pcie_core_window_` is valid.
  size_t ring_index_size_ = 0;
  volatile std::atomic<uint16_t>* d2h_read_indices_ = nullptr;
  volatile std::atomic<uint16_t>* d2h_write_indices_ = nullptr;
  volatile std::atomic<uint16_t>* h2d_read_indices_ = nullptr;
  volatile std::atomic<uint16_t>* h2d_write_indices_ = nullptr;
  volatile std::atomic<uint32_t>* h2d_write_signal_ = nullptr;

  // Our ring instances.
  std::unique_ptr<WriteDmaRing> control_submit_ring_;
  std::unique_ptr<WriteDmaRing> rx_buffer_submit_ring_;
  std::unique_ptr<ReadDmaRing> control_complete_ring_;
  std::unique_ptr<ReadDmaRing> tx_complete_ring_;
  std::unique_ptr<ReadDmaRing> rx_complete_ring_;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_RING_PROVIDER_H_
