// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DMA_POOL_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DMA_POOL_H_

#include <zircon/types.h>

#include <atomic>
#include <memory>
#include <vector>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_buffer.h"

namespace wlan {
namespace brcmfmac {

// This class manages a set of memory buffers that may be mapped for CPU access and/or passed to a
// device for DMA access.  A lease on a memory buffer is held using a Buffer instance, which holds
// that ownership in a RAII fashion.  Buffer instances can then be mapped (for CPU access) or pinned
// (for DMA access).  When the Buffer is destroyed, the underlying buffer is returned to the pool.
//
// The intended usage pattern for CPU to device transfers:
//
// * CPU leases a buffer with DmaPool::Allocate(), which returns a Buffer.
// * CPU maps the buffer for writing with Buffer::MapWrite().
// * CPU pins the buffer for DMA access with Buffer::Pin().
// * CPU sends the DMA command using the DMA address of the buffer.
//   * At this point, if the CPU wishes to stop tracking the buffer, it can release it with
//     Buffer::Release().
//   * After the DMA device finishes using the buffer in this case, the CPU then re-acquires the
//     buffer with DmaPool::Acquire().
// * CPU returns the buffer to the DmaPool using Buffer::Reset(), or by destroying the Buffer
//   instance.
//
// For device to CPU transfers:
//
// * CPU leases a buffer with DmaPool::Alocate(), which returns a Buffer.
// * CPU pins the buffer for DMA access with Buffer::Pin().
// * CPU sends the DMA command using the DMA address of the buffer.
//   * At this point, if the CPU wishes to stop tracking the buffer, it can release it with
//     Buffer::Release().
//   * After the DMA device finishes using the buffer in this case, the CPU then re-acquires the
//     buffer with DmaPool::Acquire().
// * CPU maps the buffer for reading with Buffer::MapRead().
// * CPU returns the buffer to the DmaPool using Buffer::Reset(), or by destroying the Buffer
//   instance.
//
// Thread-safety: The DmaPool class is thread-safe for allocating, acquiring, and releasing Buffer
// instances.  Individual Buffers are not thread-safe.
class DmaPool {
 public:
  class Buffer {
   public:
    // Buffer instances are moved, not copied.
    Buffer();
    Buffer(const Buffer& other) = delete;
    Buffer(Buffer&& other);
    Buffer& operator=(Buffer other);
    friend void swap(Buffer& lhs, Buffer& rhs);
    ~Buffer();

    // State accessors.
    bool is_valid() const;
    int index() const;

    // Map this Buffer for CPU access.
    zx_status_t MapRead(size_t read_size, const void** out_data);
    zx_status_t MapWrite(size_t write_size, void** out_data);

    // Pin this buffer for device access.
    zx_status_t Pin(zx_paddr_t* out_dma_address);

    // Release ownership of this Buffer without returning the underlying buffer to the DmaPool yet.
    // This is done for example when a buffer is sent to the device, and we wish to stop tracking
    // it; later when the device returns our buffer it must be reclaimed later using
    // DmaPool::Acquire().
    void Release();

    // Reset this Buffer, returning the underlying buffer to the DmaPool if it is valid.
    void Reset();

   private:
    friend class DmaPool;
    explicit Buffer(DmaPool* parent, int index, size_t read_size, size_t write_size);

    DmaPool* parent_ = nullptr;

    static constexpr int kInvalidIndex = -1;
    int index_ = kInvalidIndex;

    // Read and write indices that track the amount of cache read/write data that needs to be
    // invalidated from or flushed to the device.
    size_t read_size_ = 0;
    size_t write_size_ = 0;
  };

  // DmaPool can be moved, but not copied.
  DmaPool(const DmaPool& other) = delete;
  DmaPool(DmaPool&& other);
  DmaPool& operator=(DmaPool other);
  friend void swap(DmaPool& lhs, DmaPool& rhs);
  ~DmaPool();

  // Static factory function for DmaPool instances.  The DmaPool will provide `buffer_count`
  // buffers, each of `buffer_size` bytes, backed by `dma_buffer`.
  static zx_status_t Create(size_t buffer_size, int buffer_count,
                            std::unique_ptr<DmaBuffer> dma_buffer,
                            std::unique_ptr<DmaPool>* out_dma_pool);

  // State accessors.

  // Size of each buffer provided by this DmaPool, in bytes.
  size_t buffer_size() const;

  // Number of buffers provided by this DmaPool.  The buffers will be indexed on the range
  // [0, buffer_count()).
  int buffer_count() const;

  // Allocate a Buffer instance.  It is regarded as empty, so its existing contents are ignored.
  // The lifetime of the DmaPool must exceed the returned Buffer.
  zx_status_t Allocate(Buffer* out_buffer);

  // Acquire a Buffer instance of index `index` that was previously released.  The buffer may have
  // previously been given to a DMA device, so its existing contents are acquired and preserved.
  // The lifetime of the DmaPool must exceed the returned Buffer.
  zx_status_t Acquire(int index, Buffer* out_buffer);

 private:
  // Internal buffer record-keeping struct.
  struct Record {
    enum State {
      kInvalid = 0,
      kFree,
      kAllocated,
      kReleased,
    };

    Record* next_free = nullptr;
    State state = State::kInvalid;
  };

  // Tagged pointer for the list head.
  struct ListHead {
    Record* record = nullptr;
    uintptr_t aba_state = 0;
  };

  DmaPool();

  // Get the appropriate address for the buffer, by index.
  void* GetAddress(int index) const;
  zx_paddr_t GetDmaAddress(int index) const;

  // Release a buffer by index.  The buffer is now still allocated, but unbound to a Buffer
  // instance.
  void Release(int index);

  // Return a buffer by index to the pool, that was returned from Allocate() or Acquire().
  void Return(int index);

  size_t buffer_size_ = 0;
  int buffer_count_ = 0;
  std::unique_ptr<DmaBuffer> dma_allocation_;
  std::vector<Record> records_;
  std::atomic<ListHead> next_free_record_ = {};
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DMA_POOL_H_
