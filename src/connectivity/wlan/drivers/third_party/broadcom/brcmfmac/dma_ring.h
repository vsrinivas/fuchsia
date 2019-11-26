// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DMA_RING_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DMA_RING_H_

// This file implements DMA ringbuffers for use with the brcmfmac DMA queues.  These ringbuffers are
// a fixed size, operating on fixed-size elements.  This implementation provides a cached-memory
// view of the underlying DMA buffer, and it handles the appropriate cache cleaning and invalidation
// internally.  Note that this implementation also does not provide automatic wrap-around; if the
// free space in the ring crosses the end boundary, then the free space will have to be accessed
// twice (once for the free space to the end, and then once again for the free space from the
// beginning).

#include <zircon/types.h>

#include <atomic>
#include <memory>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_buffer.h"

namespace wlan {
namespace brcmfmac {

// This is the base DMA ring implementation, the base class for ReadDmaRing and WriteDmaRing.  The
// useful public API can be found in its subclasses.
class BaseDmaRing {
 public:
  ~BaseDmaRing();

  // State accessors.
  size_t item_size() const;
  uint16_t capacity() const;
  zx_paddr_t dma_address() const;

 protected:
  explicit BaseDmaRing(std::unique_ptr<DmaBuffer> dma_buffer, size_t item_size,
                       uint16_t item_capacity, volatile std::atomic<uint16_t>* read_index,
                       volatile std::atomic<uint16_t>* write_index);

  std::unique_ptr<DmaBuffer> dma_buffer_;
  const size_t item_size_ = 0;
  const uint16_t item_capacity_ = 0;
  volatile std::atomic<uint16_t>* const read_index_ = nullptr;
  volatile std::atomic<uint16_t>* const write_index_ = nullptr;
};

// This is the DMA read ring implementation.
class ReadDmaRing : public BaseDmaRing {
 public:
  ~ReadDmaRing();

  // Static factory function for ReadDmaRing instances.
  //
  // * `buffer` is the VMO containing the underlying memory buffer the ringbuffer will operate on.
  // * `item_size` and `item_capacity` are the size and count of the ringbuffer items.
  // * `read_index` and `write_index` are pointers to memory location (usually MMIO registers)
  //   containing the hardware's view of available ringbuffer items.
  static zx_status_t Create(std::unique_ptr<DmaBuffer> dma_buffer, size_t item_size,
                            uint16_t item_capacity, volatile std::atomic<uint16_t>* read_index,
                            volatile std::atomic<uint16_t>* write_index,
                            std::unique_ptr<ReadDmaRing>* out_read_dma_ring);

  // The read functionality comes in a pair of MapRead() and CommitRead() functions:
  //
  // * MapRead() maps `item_count` items from the ringbuffer for CPU-visible read.
  // * CommitRead() commits `item_count` items that have been read to the ringbuffer.
  //
  // Mappings are returned starting from the last call to CommitRead(); consecutive calls to
  // MapRead() without intervening calls to CommitRead() will return buffers at the same offset.  If
  // `item_count` is greater than the number of items currently available, it can be retried again
  // with a smaller count.
  uint16_t GetAvailableReads();
  zx_status_t MapRead(uint16_t item_count, const void** out_buffer);
  zx_status_t CommitRead(uint16_t item_count);

 private:
  explicit ReadDmaRing(std::unique_ptr<DmaBuffer> dma_buffer, size_t item_size,
                       uint16_t item_capacity, volatile std::atomic<uint16_t>* read_index,
                       volatile std::atomic<uint16_t>* write_index);

  // Internal implementation, also returning the read index, as an optimization to avoid having to
  // access an atomic volatile counter twice.
  uint16_t GetAvailableReads(uint16_t* out_read_index);

  // The beginning of the region that will have to be cache-invalidated to pick up new device data.
  uint16_t cache_invalidate_index_ = 0;
};

// This the the DMA write ring implementation.
class WriteDmaRing : public BaseDmaRing {
 public:
  ~WriteDmaRing();

  // Static factory function for WriteDmaRing instances.
  //
  // * `buffer` is the VMO containing the underlying memory buffer the ringbuffer will operate on.
  // * `item_size` and `item_capacity` are the size and count of the ringbuffer items.
  // * `read_index` and `write_index` are pointers to memory location (usually MMIO registers)
  //   containing the hardware's view of available ringbuffer items.
  // * `signal` is a pointer to a memory location (usually an MMIO register) which will be written
  //   to signal the hardware after the CPU has written to the ringbuffer.
  static zx_status_t Create(std::unique_ptr<DmaBuffer> dma_buffer, size_t item_size,
                            uint16_t item_capacity, volatile std::atomic<uint16_t>* read_index,
                            volatile std::atomic<uint16_t>* write_index,
                            volatile std::atomic<uint32_t>* write_signal,
                            std::unique_ptr<WriteDmaRing>* out_write_dma_ring);

  // The write functionality comes in a pair of MapWrite() and CommitWrite() functions:
  //
  // * MapWrite() maps `item_count` items from the ringbuffer for CPU-visible write.
  // * CommitWrite() commits `item_count` items that have been written to the ringbuffer.
  //
  // Mappings are returned starting from the last call to CommitWrite(); consecutive calls to
  // MapWrite() without intervening calls to CommitWrite() will return buffers at the same offset.
  // If `item_count` is greater than the number of items currently available, it can be retried
  // again with a smaller count.
  uint16_t GetAvailableWrites();
  zx_status_t MapWrite(uint16_t item_count, void** out_buffer);
  zx_status_t CommitWrite(uint16_t item_count);

 private:
  explicit WriteDmaRing(std::unique_ptr<DmaBuffer> dma_buffer, size_t item_size,
                        uint16_t item_capacity, volatile std::atomic<uint16_t>* read_index,
                        volatile std::atomic<uint16_t>* write_index,
                        volatile std::atomic<uint32_t>* write_signal);

  // Internal implementation, also returning the write index, as an optimization to avoid having to
  // access an atomic volatile counter twice.
  uint16_t GetAvailableWrites(uint16_t* out_write_index);

  // Memory-mapped register location to signal new data written to the ring.
  volatile std::atomic<uint32_t>* write_signal_ = nullptr;

  // The end of the region that will have to be cache cleaned to write new data to the device.
  uint16_t cache_clean_index_ = 0;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DMA_RING_H_
