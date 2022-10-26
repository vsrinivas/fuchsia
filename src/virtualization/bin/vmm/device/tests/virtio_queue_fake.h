// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_TESTS_VIRTIO_QUEUE_FAKE_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_TESTS_VIRTIO_QUEUE_FAKE_H_

#include <optional>

#include <virtio/virtio_ring.h>

#include "src/virtualization/bin/vmm/device/virtio_queue.h"

// Fake Virtio queue for out-of-process devices.
class VirtioQueueFake {
 public:
  struct DriverMemRange {
    zx_gpaddr_t begin;
    zx_gpaddr_t end;
  };

  struct AllocResult {
    void* device_mem;
    zx_gpaddr_t driver_mem;
  };

  VirtioQueueFake(const PhysMem& phys_mem, zx_gpaddr_t addr, uint16_t size);

  uint16_t size() const { return ring_.size; }
  zx_gpaddr_t desc() const { return desc_; }
  zx_gpaddr_t avail() const { return avail_; }
  zx_gpaddr_t used() const { return used_; }
  zx_gpaddr_t end() const { return end_; }
  DriverMemRange data() const { return DriverMemRange{data_begin_, data_end_}; }

  void Configure(zx_gpaddr_t data_addr, size_t data_len);

  AllocResult AllocData(size_t len);

  // Returns the used element structure for the next used descriptor.
  //
  // If there are no elements in the used ring, |std::nullopt| is returned.
  // Otherwise a pair with the first element holding the descriptor id and the
  // second element holding the 'len' field is returned.
  struct UsedElement {
    // The ID of the descriptor written to the used ring.
    uint32_t id;
    // The number of bytes written to the descriptor chain, as specified in
    // the used ring.
    size_t len;
  };
  std::optional<UsedElement> NextUsed();

 private:
  const PhysMem& phys_mem_;
  const zx_gpaddr_t desc_;
  const zx_gpaddr_t avail_;
  const zx_gpaddr_t used_;
  const zx_gpaddr_t end_;

  VirtioRing ring_ = {};
  zx_gpaddr_t data_begin_ = 0;
  zx_gpaddr_t data_end_ = 0;
  uint16_t next_desc_ = 0;
  uint16_t used_index_ = 0;

  zx_status_t WriteDesc(void** buf, uint32_t len, uint16_t flags, uint16_t* desc_idx);
  void WriteAvail(uint16_t head_idx);
  zx_status_t SetNext(uint16_t desc_idx, uint16_t next_idx);

  friend class DescriptorChainBuilder;
};

// Helper class to build descriptor chains for Virtio queues.
class DescriptorChainBuilder {
 public:
  DescriptorChainBuilder(VirtioQueueFake& queue_fake);

  DescriptorChainBuilder& AppendReadableDescriptor(const void* buf, uint32_t len);
  DescriptorChainBuilder& AppendWritableDescriptor(void** buf, uint32_t len);

  template <typename T>
  DescriptorChainBuilder& AppendWritableDescriptor(T** ptr, uint32_t len) {
    return AppendWritableDescriptor(reinterpret_cast<void**>(ptr), len);
  }

  // Builds the descritpor chain and writes the head index into the avail ring.
  //
  // The index of the head descriptor of the chain is written to |index| if it's
  // non-null.
  zx_status_t Build(uint16_t* index = nullptr);

  DescriptorChainBuilder& AppendDescriptor(void** buf, uint32_t len, uint16_t flags);

 private:
  VirtioQueueFake& queue_fake_;
  size_t chain_len_ = 0;
  uint16_t prev_idx_ = 0;
  uint16_t head_idx_ = 0;
  zx_status_t status_ = ZX_OK;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_TESTS_VIRTIO_QUEUE_FAKE_H_
