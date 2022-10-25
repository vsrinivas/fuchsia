// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_GVNIC_GVNIC_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_GVNIC_GVNIC_H_

#include <lib/device-protocol/pci.h>
#include <lib/dma-buffer/buffer.h>
#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>

#include "src/connectivity/ethernet/drivers/gvnic/abi.h"
#include "src/connectivity/ethernet/drivers/gvnic/pagelist.h"

#define GVNIC_VERSION "v1.awesome"

namespace gvnic {

class Gvnic;
using DeviceType = ddk::Device<Gvnic, ddk::Initializable>;
class Gvnic : public DeviceType {
 public:
  explicit Gvnic(zx_device_t* parent) : DeviceType(parent) {}
  virtual ~Gvnic() = default;

  static __WARN_UNUSED_RESULT zx_status_t Bind(void* ctx, zx_device_t* dev);
  __WARN_UNUSED_RESULT zx_status_t Bind();

  // ::ddk::Device implementation.
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();

  // For inspect test.
  zx::vmo inspect_vmo() { return inspect_.DuplicateVmo(); }

 private:
  __WARN_UNUSED_RESULT zx_status_t SetUpPci();
  __WARN_UNUSED_RESULT zx_status_t MapBars();
  __WARN_UNUSED_RESULT zx_status_t ResetCard(bool use_new_reset_sequence);
  __WARN_UNUSED_RESULT zx_status_t WriteVersion(const char* ver, uint32_t len);
  __WARN_UNUSED_RESULT zx_status_t CreateAdminQueue();
  __WARN_UNUSED_RESULT zx_status_t EnableCard();
  __WARN_UNUSED_RESULT zx_status_t DescribeDevice();
  __WARN_UNUSED_RESULT zx_status_t ReportLinkSpeed();
  __WARN_UNUSED_RESULT zx_status_t ConfigureDeviceResources();
  __WARN_UNUSED_RESULT zx_status_t RegisterPageList(std::unique_ptr<PageList>& page_list);
  __WARN_UNUSED_RESULT zx_status_t CreateTXQueue();
  __WARN_UNUSED_RESULT zx_status_t CreateRXQueue();

  GvnicAdminqEntry* NextAdminQEntry();
  void SubmitPendingAdminQEntries(bool wait);
  void WaitForAdminQueueCompletion();

  uint32_t GetNextFreeDoorbellIndex();
  uint32_t GetNextQueueResourcesIndex();
  zx_paddr_t GetQueueResourcesPhysAddr(uint32_t index);
  GvnicQueueResources* GetQueueResourcesVirtAddr(uint32_t index);

  void WriteDoorbell(uint32_t index, uint32_t value);
  uint32_t ReadCounter(uint32_t index);

  ddk::Pci pci_;
  std::unique_ptr<dma_buffer::BufferFactory> buffer_factory_;
  std::optional<fdf::MmioBuffer> reg_mmio_;
  std::optional<fdf::MmioBuffer> doorbell_mmio_;

  GvnicRegisters regs_;
  zx::bti bti_;

  std::unique_ptr<dma_buffer::ContiguousBuffer> admin_queue_;
  uint32_t admin_queue_index_;          // Index of the next usable entry
  uint32_t admin_queue_num_allocated_;  // Allocated, but not submitted
  uint32_t admin_queue_num_pending_;    // Submitted, but not finished

  std::unique_ptr<dma_buffer::ContiguousBuffer> scratch_page_;
  std::unique_ptr<dma_buffer::ContiguousBuffer> counter_page_;

  // ConfigureDeviceResources(), will cause the card to allocate some IRQ doorbells. It will need
  // to report which indices were allocated so that this driver can avoid allocating an index that
  // was already allocated. This buffer is how that information will be reported.
  std::unique_ptr<dma_buffer::ContiguousBuffer> irq_doorbell_idx_page_;
  uint32_t irq_doorbell_idx_stride_;  // Measuered in entries, not bytes.
  uint32_t num_irq_doorbell_idxs_;

  uint32_t next_doorbell_idx_;  // Index of the next potentially-allocatable doorbell.
  uint32_t max_doorbells_;      // The max doorbells expected to ever allocate (IRQ, RX, and TX)

  GvnicDeviceDescriptor dev_descr_;
  std::unique_ptr<GvnicDeviceOption[]> dev_opts_;
  uint64_t link_speed_;

  std::unique_ptr<PageList> tx_page_list_;
  std::unique_ptr<PageList> rx_page_list_;

  std::unique_ptr<dma_buffer::ContiguousBuffer> queue_resources_;
  uint32_t next_queue_resources_index_;
  GvnicQueueResources* tx_queue_resources_;
  GvnicQueueResources* rx_queue_resources_;

  std::unique_ptr<dma_buffer::ContiguousBuffer> tx_ring_;
  // The RX side has two rings, one for the driver to send empty buffers to the device, and one for
  // the device to send back filled in buffers.
  std::unique_ptr<dma_buffer::ContiguousBuffer> rx_desc_ring_;  // From NIC
  std::unique_ptr<dma_buffer::ContiguousBuffer> rx_data_ring_;  // To NIC

  // Each incoming packet needs space allocated. The packets are at most MTU bytes long. There is
  // also a 2 byte padding at the start of each packet for alignment reasons (the Ethernet header is
  // 14 bytes). And packets should not share a cache line with any other packet. So, this is the
  // MTU, plus the 2 byte padding, rounded up to the nearest cache line.
  uint16_t rounded_mtu_;
  uint16_t rx_ring_len_;

  uint16_t tx_ring_len_;

  inspect::Inspector inspect_;
  // `is_bound` is an example property. Replace this with useful properties of the device.
  inspect::BoolProperty is_bound = inspect_.GetRoot().CreateBool("is_bound", false);
};

}  // namespace gvnic

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_GVNIC_GVNIC_H_
