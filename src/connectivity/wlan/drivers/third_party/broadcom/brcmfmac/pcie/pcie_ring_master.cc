// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_ring_master.h"

#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_buffer.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_ring.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_firmware.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_regs.h"

namespace wlan {
namespace brcmfmac {

// DMA ring configuration information.
struct PcieRingMaster::DmaRingConfig {
  int ring_index = -1;   // Sequential index of the ring.  D2H and H2D rings are indexed separately.
  size_t item_size = 0;  // Size of items on the ring.
  uint16_t item_capacity = 0;  // Count of items in the ring.
};

namespace {

constexpr int kHostToDeviceRingCount = 2;
constexpr int kDeviceToHostRingCount = 3;
constexpr uint16_t kDmaIndexSupported = 0x0001;
constexpr uint16_t kDmaIndex2Bytes = 0x0010;

constexpr int kMaxIoctlRxBuffers = 8;
constexpr int kMaxEventRxBuffers = 8;

// Dongle/host interface shared ring informational struct.  Copied to/from shared RAM.
struct [[gnu::packed]] DhiRingInfo {
  uint32_t ringmem;
  uint32_t h2d_w_idx_ptr;
  uint32_t h2d_r_idx_ptr;
  uint32_t d2h_w_idx_ptr;
  uint32_t d2h_r_idx_ptr;
  uint64_t h2d_w_idx_hostaddr;
  uint64_t h2d_r_idx_hostaddr;
  uint64_t d2h_w_idx_hostaddr;
  uint64_t d2h_r_idx_hostaddr;
  uint16_t max_flow_rings;
  uint16_t max_submission_rings;
  uint16_t max_completion_rings;
};

// Ringbuffer state struct.  Copied to/from shared RAM.
struct [[gnu::packed]] RingState {
  uint32_t pad0;
  uint16_t max_item;
  uint16_t len_items;
  uint64_t mem_base_addr;
};

// Configuration values for our rings.
constexpr PcieRingMaster::DmaRingConfig kControlSubmitRingConfig = {
    .ring_index = 0,
    .item_size = 40,
    .item_capacity = 64,
};

constexpr PcieRingMaster::DmaRingConfig kRxBufferSubmitRingConfig = {
    .ring_index = 1,
    .item_size = 32,
    .item_capacity = 512,
};

constexpr PcieRingMaster::DmaRingConfig kTxFlowRingConfig = {
    .ring_index = 2,  // This the offset of flow ring 0; subsequent rings increment this index.
    .item_size = 48,
    .item_capacity = 512,
};

constexpr PcieRingMaster::DmaRingConfig kControlCompleteRingConfig = {
    .ring_index = 0,
    .item_size = 24,
    .item_capacity = 64,
};

constexpr PcieRingMaster::DmaRingConfig kTxCompleteRingConfig = {
    .ring_index = 1,
    .item_size = 16,
    .item_capacity = 1024,
};

constexpr PcieRingMaster::DmaRingConfig kRxCompleteRingConfig = {
    .ring_index = 2,
    .item_size = 32,
    .item_capacity = 512,
};

// Update the DMA ring config in shared RAM.  Only used for the submit and complete rings; flow
// rings are dynamically configured using control messages.
void UpdateRingState(PcieBuscore* buscore, uint32_t ring_state_offset,
                     const BaseDmaRing& dma_ring) {
  // Update the state in shared RAM.
  auto ring_state = std::make_unique<RingState>();
  buscore->TcmRead(ring_state_offset, ring_state.get(), sizeof(*ring_state));
  ring_state->max_item = dma_ring.capacity();
  ring_state->len_items = dma_ring.item_size();
  ring_state->mem_base_addr = dma_ring.dma_address();
  buscore->TcmWrite(ring_state_offset, ring_state.get(), sizeof(*ring_state));
}

}  // namespace

PcieRingMaster::PcieRingMaster() = default;

PcieRingMaster::~PcieRingMaster() = default;

// static
zx_status_t PcieRingMaster::Create(PcieBuscore* buscore, PcieFirmware* firmware,
                                   std::unique_ptr<PcieRingMaster>* out_ring_master) {
  zx_status_t status = ZX_OK;

  // Read from shared memory for the ring configuration.
  int max_submission_rings = 0;
  int max_flow_rings = 0;
  int max_completion_rings = 0;
  auto ringinfo = std::make_unique<DhiRingInfo>();
  buscore->TcmRead(firmware->GetRingInfoOffset(), ringinfo.get(), sizeof(*ringinfo));

  if (firmware->GetSharedRamVersion() >= 6) {
    max_submission_rings = ringinfo->max_submission_rings;
    max_flow_rings = ringinfo->max_flow_rings;
    max_completion_rings = ringinfo->max_completion_rings;
  } else {
    max_submission_rings = kHostToDeviceRingCount;
    max_flow_rings = ringinfo->max_flow_rings - kHostToDeviceRingCount;
    max_completion_rings = kDeviceToHostRingCount;
  }

  DmaConfig dma_config = {};
  dma_config.max_flow_rings = max_flow_rings;
  dma_config.flow_ring_offset = kTxFlowRingConfig.ring_index;
  dma_config.max_ioctl_rx_buffers = kMaxIoctlRxBuffers;
  dma_config.max_event_rx_buffers = kMaxEventRxBuffers;
  dma_config.max_rx_buffers = firmware->GetMaxRxbufpost();
  dma_config.rx_data_offset = firmware->GetRxDataOffset();

  // Set up the DMA ringbuffer index memory regions.
  size_t ring_index_size = 0;
  volatile void* d2h_read_indices = nullptr;
  volatile void* d2h_write_indices = nullptr;
  volatile void* h2d_read_indices = nullptr;
  volatile void* h2d_write_indices = nullptr;
  std::unique_ptr<DmaBuffer> index_buffer;
  if ((firmware->GetSharedRamFlags() & kDmaIndexSupported) == 0) {
    // Using TCM indices.
    ring_index_size = sizeof(uint32_t);
    d2h_read_indices = buscore->GetTcmPointer(ringinfo->d2h_r_idx_ptr);
    d2h_write_indices = buscore->GetTcmPointer(ringinfo->d2h_w_idx_ptr);
    h2d_read_indices = buscore->GetTcmPointer(ringinfo->h2d_r_idx_ptr);
    h2d_write_indices = buscore->GetTcmPointer(ringinfo->h2d_w_idx_ptr);
  } else {
    // Using the index buffer and host memory indices.
    if ((firmware->GetSharedRamFlags() & kDmaIndex2Bytes) != 0) {
      ring_index_size = sizeof(uint16_t);
    } else {
      ring_index_size = sizeof(uint32_t);
    }
    const size_t index_buffer_size =
        (max_submission_rings + max_completion_rings) * ring_index_size * 2;
    if ((status = buscore->CreateDmaBuffer(ZX_CACHE_POLICY_UNCACHED_DEVICE, index_buffer_size,
                                           &index_buffer)) != ZX_OK) {
      BRCMF_ERR("Failed to create index buffer, size %zu: %s\n", index_buffer_size,
                zx_status_get_string(status));
      return status;
    }
    const uintptr_t cpu_index_address = index_buffer->address();
    const zx_paddr_t dma_index_address = index_buffer->dma_address();
    size_t index_offset = 0;

    // Set up the ring read and write indices.
    d2h_read_indices = reinterpret_cast<volatile void*>(cpu_index_address + index_offset);
    ringinfo->d2h_r_idx_hostaddr = dma_index_address + index_offset;
    index_offset += max_completion_rings * ring_index_size;

    d2h_write_indices = reinterpret_cast<volatile void*>(cpu_index_address + index_offset);
    ringinfo->d2h_w_idx_hostaddr = dma_index_address + index_offset;
    index_offset += max_completion_rings * ring_index_size;

    h2d_read_indices = reinterpret_cast<volatile void*>(cpu_index_address + index_offset);
    ringinfo->h2d_r_idx_hostaddr = dma_index_address + index_offset;
    index_offset += max_submission_rings * ring_index_size;

    h2d_write_indices = reinterpret_cast<volatile void*>(cpu_index_address + index_offset);
    ringinfo->h2d_w_idx_hostaddr = dma_index_address + index_offset;

    // Write the configuration back to the device.
    buscore->TcmWrite(firmware->GetRingInfoOffset(), ringinfo.get(), sizeof(*ringinfo));
  }

  PcieBuscore::CoreRegs pci_core_regs;
  if ((status = buscore->GetCoreRegs(CHIPSET_PCIE2_CORE, &pci_core_regs)) != ZX_OK) {
    BRCMF_ERR("Failed to get PCIE2 core regs: %s\n", zx_status_get_string(status));
    return status;
  }

  // Note that `h2d_write_signal` is only valid during the lifetime of the CoreRegs instance from
  // which it was obtained.
  volatile void* const h2d_write_signal =
      pci_core_regs.GetRegPointer(BRCMF_PCIE_PCIE2REG_H2D_MAILBOX);

  // Set up enough state on a PcieRingMaster to begin to forge rings.
  auto ring_master = std::make_unique<PcieRingMaster>();
  ring_master->buscore_ = buscore;
  ring_master->dma_config_ = dma_config;
  ring_master->index_buffer_ = std::move(index_buffer);
  ring_master->pci_core_regs_ = std::move(pci_core_regs);
  ring_master->ring_index_size_ = ring_index_size;
  ring_master->d2h_read_indices_ =
      reinterpret_cast<volatile std::atomic<uint16_t>*>(d2h_read_indices);
  ring_master->d2h_write_indices_ =
      reinterpret_cast<volatile std::atomic<uint16_t>*>(d2h_write_indices);
  ring_master->h2d_read_indices_ =
      reinterpret_cast<volatile std::atomic<uint16_t>*>(h2d_read_indices);
  ring_master->h2d_write_indices_ =
      reinterpret_cast<volatile std::atomic<uint16_t>*>(h2d_write_indices);
  ring_master->h2d_write_signal_ =
      reinterpret_cast<volatile std::atomic<uint32_t>*>(h2d_write_signal);

  uint32_t ring_state_offset = ringinfo->ringmem;

  // Three [two] rings for the Elven-kings [host to device] under the sky,
  std::unique_ptr<WriteDmaRing> control_submit_ring;
  if ((status = ring_master->CreateWriteDmaRing(kControlSubmitRingConfig, &control_submit_ring)) !=
      ZX_OK) {
    BRCMF_ERR("Failed to create control submit ring: %s\n", zx_status_get_string(status));
    return status;
  }
  UpdateRingState(buscore, ring_state_offset, *control_submit_ring);
  ring_state_offset += sizeof(RingState);
  std::unique_ptr<WriteDmaRing> rx_buffer_submit_ring;
  if ((status = ring_master->CreateWriteDmaRing(kRxBufferSubmitRingConfig,
                                                &rx_buffer_submit_ring)) != ZX_OK) {
    BRCMF_ERR("Failed to create rx buffer submit ring: %s\n", zx_status_get_string(status));
    return status;
  }
  UpdateRingState(buscore, ring_state_offset, *rx_buffer_submit_ring);
  ring_state_offset += sizeof(RingState);

  // Seven [three] for the Dwarf-lords [device to host] in their halls of stone,
  std::unique_ptr<ReadDmaRing> control_complete_ring;
  if ((status = ring_master->CreateReadDmaRing(kControlCompleteRingConfig,
                                               &control_complete_ring)) != ZX_OK) {
    BRCMF_ERR("Failed to create control complete ring: %s\n", zx_status_get_string(status));
    return status;
  }
  UpdateRingState(buscore, ring_state_offset, *control_complete_ring);
  ring_state_offset += sizeof(RingState);
  std::unique_ptr<ReadDmaRing> tx_complete_ring;
  if ((status = ring_master->CreateReadDmaRing(kTxCompleteRingConfig, &tx_complete_ring)) !=
      ZX_OK) {
    BRCMF_ERR("Failed to create tx complete ring: %s\n", zx_status_get_string(status));
    return status;
  }
  UpdateRingState(buscore, ring_state_offset, *tx_complete_ring);
  ring_state_offset += sizeof(RingState);
  std::unique_ptr<ReadDmaRing> rx_complete_ring;
  if ((status = ring_master->CreateReadDmaRing(kRxCompleteRingConfig, &rx_complete_ring)) !=
      ZX_OK) {
    BRCMF_ERR("Failed to create rx complete ring: %s\n", zx_status_get_string(status));
    return status;
  }
  UpdateRingState(buscore, ring_state_offset, *rx_complete_ring);
  ring_state_offset += sizeof(RingState);

  // Nine [max_flow_rings] for mortal men [host to device flow rings] doomed to die,
  // Note: these rings are forged on demand.

  // One for the Dark Lord [PcieRingMaster] on his dark throne
  ring_master->control_submit_ring_ = std::move(control_submit_ring);
  ring_master->rx_buffer_submit_ring_ = std::move(rx_buffer_submit_ring);
  ring_master->control_complete_ring_ = std::move(control_complete_ring);
  ring_master->tx_complete_ring_ = std::move(tx_complete_ring);
  ring_master->rx_complete_ring_ = std::move(rx_complete_ring);
  *out_ring_master = std::move(ring_master);

  // In the Land of Mordor where the shadows lie.
  return ZX_OK;
}

const PcieRingMaster::DmaConfig& PcieRingMaster::GetDmaConfig() const { return dma_config_; }

WriteDmaRing* PcieRingMaster::GetControlSubmitRing() { return control_submit_ring_.get(); }

WriteDmaRing* PcieRingMaster::GetRxBufferSubmitRing() { return rx_buffer_submit_ring_.get(); }

ReadDmaRing* PcieRingMaster::GetControlCompleteRing() { return control_complete_ring_.get(); }

ReadDmaRing* PcieRingMaster::GetTxCompleteRing() { return tx_complete_ring_.get(); }

ReadDmaRing* PcieRingMaster::GetRxCompleteRing() { return rx_complete_ring_.get(); }

zx_status_t PcieRingMaster::CreateFlowRing(int flow_ring_index,
                                           std::unique_ptr<WriteDmaRing>* out_flow_ring) {
  zx_status_t status = ZX_OK;

  if (static_cast<size_t>(flow_ring_index) >= static_cast<size_t>(dma_config_.max_flow_rings)) {
    BRCMF_ERR("Invalid flow ring index %d, max %d\n", flow_ring_index, dma_config_.max_flow_rings);
    return ZX_ERR_INVALID_ARGS;
  }

  DmaRingConfig ring_config = kTxFlowRingConfig;
  ring_config.ring_index += flow_ring_index;
  std::unique_ptr<WriteDmaRing> flow_ring;
  if ((status = CreateWriteDmaRing(ring_config, &flow_ring)) != ZX_OK) {
    BRCMF_ERR("Failed to create flow ring %d: %s\n", flow_ring_index, zx_status_get_string(status));
    return status;
  }

  *out_flow_ring = std::move(flow_ring);
  return ZX_OK;
}

zx_status_t PcieRingMaster::CreateReadDmaRing(const DmaRingConfig& config,
                                              std::unique_ptr<ReadDmaRing>* out_ring) {
  zx_status_t status = ZX_OK;
  std::unique_ptr<DmaBuffer> ring_buffer;
  const size_t ring_buffer_size = config.item_size * config.item_capacity;
  if ((status = buscore_->CreateDmaBuffer(ZX_CACHE_POLICY_CACHED, ring_buffer_size,
                                          &ring_buffer)) != ZX_OK) {
    BRCMF_ERR("Failed to create read DMA ring %d: %s\n", config.ring_index,
              zx_status_get_string(status));
    return status;
  }

  std::unique_ptr<ReadDmaRing> read_ring;
  if ((status = ReadDmaRing::Create(
           std::move(ring_buffer), config.item_size, config.item_capacity,
           d2h_read_indices_ + (config.ring_index * (ring_index_size_ / sizeof(uint16_t))),
           d2h_write_indices_ + (config.ring_index * (ring_index_size_ / sizeof(uint16_t))),
           &read_ring)) != ZX_OK) {
    return status;
  }

  *out_ring = std::move(read_ring);
  return ZX_OK;
}

zx_status_t PcieRingMaster::CreateWriteDmaRing(const DmaRingConfig& config,
                                               std::unique_ptr<WriteDmaRing>* out_ring) {
  zx_status_t status = ZX_OK;
  std::unique_ptr<DmaBuffer> ring_buffer;
  const size_t ring_buffer_size = config.item_size * config.item_capacity;
  if ((status = buscore_->CreateDmaBuffer(ZX_CACHE_POLICY_CACHED, ring_buffer_size,
                                          &ring_buffer)) != ZX_OK) {
    BRCMF_ERR("Failed to create write DMA ring %d: %s\n", config.ring_index,
              zx_status_get_string(status));
    return status;
  }

  std::unique_ptr<WriteDmaRing> write_ring;
  if ((status = WriteDmaRing::Create(
           std::move(ring_buffer), config.item_size, config.item_capacity,
           h2d_read_indices_ + (config.ring_index * (ring_index_size_ / sizeof(uint16_t))),
           h2d_write_indices_ + (config.ring_index * (ring_index_size_ / sizeof(uint16_t))),
           h2d_write_signal_, &write_ring)) != ZX_OK) {
    return status;
  }

  *out_ring = std::move(write_ring);
  return ZX_OK;
}

}  // namespace brcmfmac
}  // namespace wlan
