// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_TEST_FAKE_MSGBUF_INTERFACES_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_TEST_FAKE_MSGBUF_INTERFACES_H_

#include <lib/zx/bti.h>
#include <lib/zx/vmar.h>
#include <zircon/types.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_ring.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_interfaces.h"

namespace wlan {
namespace brcmfmac {

// This class provides testing fakes for the interfaces defined in msgbuf_interfaces.h.
//
// * DmaBufferProviderInterface
//
// The fake provided for this interface allows creation of DmaBuffer instances using fake_bti, and
// also tracks the DMA addresses of the DmaBuffers created, so that when the DMA address is provided
// back to our fake device, we can inspect the buffer contents on a CPU mapping.  This functionality
// is provided by GetDmaBufferAddress().
//
// * DmaRingProviderInterface
//
// The fake provided for this interface provides a callback-based mechanism for parsing DMA ring
// entries as they arrive.  Tests can register these callbacks through
// Add{Control,RxBuffer}SubmitRingCallback() for each respective ring.
//
// * InterruptProviderInterface
//
// The fake provided for this interfaces polls "host" state and fires the interrupt handlers when
// the appropriate conditions are met.  It also provides "device"-side buffer recycling
// functionality.
class FakeMsgbufInterfaces : public DmaBufferProviderInterface,
                             public DmaRingProviderInterface,
                             public InterruptProviderInterface {
 public:
  // Struct tracking the device-side state of a DmaPool buffer.
  struct DmaPoolBuffer {
    int index = 0;
    uintptr_t address = 0;
    zx_paddr_t dma_address = 0;
    size_t size = 0;
  };

  ~FakeMsgbufInterfaces() override;

  // Static factory function for FakeMsgbufInterfaces() instances.
  static zx_status_t Create(std::unique_ptr<FakeMsgbufInterfaces>* out_interfaces);

  // Interface implementations.

  // DmaBufferProviderInterface implementation.
  zx_status_t CreateDmaBuffer(uint32_t cache_policy, size_t size,
                              std::unique_ptr<DmaBuffer>* out_dma_buffer) override;

  // InterruptHandlerProviderInterface implementation.
  const DmaConfig& GetDmaConfig() const override;
  WriteDmaRing* GetControlSubmitRing() override;
  WriteDmaRing* GetRxBufferSubmitRing() override;
  ReadDmaRing* GetControlCompleteRing() override;
  ReadDmaRing* GetTxCompleteRing() override;
  ReadDmaRing* GetRxCompleteRing() override;
  virtual zx_status_t CreateFlowRing(int flow_ring_index,
                                     std::unique_ptr<WriteDmaRing>* out_flow_ring) override;

  // InterruptProviderInterface implementation.
  zx_status_t AddInterruptHandler(InterruptHandler* handler) override;
  zx_status_t RemoveInterruptHandler(InterruptHandler* handler) override;

  // Test utility functions.

  // Get a CPU address for a DMA address from a DmaBuffer created by CreateDmaBuffer().  This CPU
  // address is not the same one from DmaBuffer::Map(); it is valid but intended for test use.  If
  // no mapping is found, returns 0.
  uintptr_t GetDmaBufferAddress(zx_paddr_t dma_address);

  // Add a callback to the activity queue on each respective WriteDmaRing.  When data is written and
  // becomes available on the ring, the next callback in the queue is invoked, then discarded.
  void AddControlSubmitRingCallback(std::function<void(const void* buffer, size_t size)> callback);
  void AddRxBufferSubmitRingCallback(std::function<void(const void* buffer, size_t size)> callback);
  void AddFlowRingCallback(int flow_ring_index,
                           std::function<void(const void* buffer, size_t size)> callback);

  // Add data to the respective rings.  The ring pointers are incremented after the access is
  // performed.
  zx_status_t AddControlCompleteRingEntry(const void* buffer, size_t size);
  zx_status_t AddTxCompleteRingEntry(const void* buffer, size_t size);
  zx_status_t AddRxCompleteRingEntry(const void* buffer, size_t size);

  // Get a free buffer of the respective type, if available.  If none are available, the returned
  // entry will be empty.
  DmaPoolBuffer GetIoctlRxBuffer();
  DmaPoolBuffer GetEventRxBuffer();
  DmaPoolBuffer GetRxBuffer();

 private:
  // Utility structs to hold a ring and its associated state.
  struct SubmitRing {
    std::atomic<uint16_t> read_index = 0;
    std::atomic<uint16_t> write_index = 0;
    std::unique_ptr<WriteDmaRing> ring;
  };
  struct CompleteRing {
    std::atomic<uint16_t> read_index = 0;
    std::atomic<uint16_t> write_index = 0;
    std::unique_ptr<ReadDmaRing> ring;
  };
  struct FlowRing {
    std::atomic<uint16_t> read_index = 0;
    std::atomic<uint16_t> write_index = 0;
    // We don't store the ring itself, since we don't own it.  Holding onto its DMA address here is
    // actually a resource leak, but for testing purposes that is sufficient for now.
    zx_paddr_t dma_address = 0;
  };

  // Create a ring and its associated state.
  static zx_status_t CreateSubmitRing(DmaBufferProviderInterface* provider, size_t item_size,
                                      int capacity, std::atomic<uint32_t>* write_signal,
                                      std::unique_ptr<SubmitRing>* out_ring);
  static zx_status_t CreateCompleteRing(DmaBufferProviderInterface* provider, size_t item_size,
                                        int capacity, std::unique_ptr<CompleteRing>* out_ring);

  FakeMsgbufInterfaces();

  // Execution function of the interrupt thread.
  void InterruptThread();

  // Process an entry on the respective ring.  Returns true iff the entry should be consumed (i.e.
  // not pased on to the ring callback).
  bool ProcessControlSubmitRingEntry(const void* data, size_t size);
  bool ProcessRxBufferSubmitRingEntry(const void* data, size_t size);
  bool ProcessFlowRingEntry(int flow_ring_index, const void* data, size_t size);

  // Add an entry to a completion ring.
  zx_status_t AddCompleteRingEntry(CompleteRing* ring, const void* buffer, size_t size);

  // Virtual memory support objects for CreateDmaBuffer().
  zx::bti bti_;
  zx::vmar vmar_;
  std::mutex dma_mutex_;
  zx_paddr_t next_dma_address_ = 0x1000;
  std::map<zx_paddr_t, std::pair<size_t, uintptr_t>> dma_buffer_permanent_mapping_;

  DmaConfig dma_config_ = {};

  // Ring states.
  std::unique_ptr<SubmitRing> control_submit_ring_;
  std::unique_ptr<SubmitRing> rx_buffer_submit_ring_;
  std::unique_ptr<CompleteRing> control_complete_ring_;
  std::unique_ptr<CompleteRing> tx_complete_ring_;
  std::unique_ptr<CompleteRing> rx_complete_ring_;
  std::vector<FlowRing> flow_rings_;
  std::atomic<uint32_t> submit_ring_write_signal_ = 0;
  std::atomic<uint32_t> complete_ring_write_signal_ = 0;
  std::list<std::function<void(const void* buffer, size_t size)>> control_submit_ring_callbacks_;
  std::list<std::function<void(const void* buffer, size_t size)>> rx_buffer_submit_ring_callbacks_;
  std::vector<std::list<std::function<void(const void* buffer, size_t size)>>> flow_ring_callbacks_;

  // Interrupt processing state.
  std::mutex interrupt_mutex_;
  std::thread interrupt_thread_;
  std::atomic<bool> thread_exit_flag_ = false;
  std::list<InterruptHandler*> interrupt_handlers_;

  // Free buffer queues.
  std::mutex rx_buffers_mutex_;
  std::condition_variable rx_buffers_condvar_;
  std::list<DmaPoolBuffer> ioctl_rx_buffers_;
  std::list<DmaPoolBuffer> event_rx_buffers_;
  std::list<DmaPoolBuffer> rx_buffers_;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_TEST_FAKE_MSGBUF_INTERFACES_H_
