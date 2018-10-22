// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_DEVICE_VIRTIO_QUEUE_FAKE_H_
#define GARNET_BIN_GUEST_VMM_DEVICE_VIRTIO_QUEUE_FAKE_H_

#include "garnet/lib/machina/device/virtio_queue.h"

// Fake Virtio queue for out-of-process devices.
class VirtioQueueFake {
 public:
  VirtioQueueFake(const machina::PhysMem& phys_mem, zx_gpaddr_t addr,
                  uint16_t size);

  uint16_t size() const { return ring_.size; }
  zx_gpaddr_t desc() const { return desc_; }
  zx_gpaddr_t avail() const { return avail_; }
  zx_gpaddr_t used() const { return used_; }
  zx_gpaddr_t end() const { return end_; }

  void Configure(zx_gpaddr_t data_addr, size_t data_len);

 private:
  const machina::PhysMem& phys_mem_;
  const zx_gpaddr_t desc_;
  const zx_gpaddr_t avail_;
  const zx_gpaddr_t used_;
  const zx_gpaddr_t end_;

  machina::VirtioRing ring_ = {};
  zx_gpaddr_t data_begin_ = 0;
  zx_gpaddr_t data_end_ = 0;
  uint16_t next_desc_ = 0;

  zx_status_t WriteDesc(void** buf, uint32_t len, uint16_t flags,
                        uint16_t* desc_idx);
  void WriteAvail(uint16_t head_idx);
  zx_status_t SetNext(uint16_t desc_idx, uint16_t next_idx);

  friend class DescriptorChainBuilder;
};

// Helper class to build descriptor chains for Virtio queues.
class DescriptorChainBuilder {
 public:
  DescriptorChainBuilder(VirtioQueueFake& queue_fake);

  DescriptorChainBuilder& AppendReadableDescriptor(const void* buf,
                                                   uint32_t len);
  DescriptorChainBuilder& AppendWritableDescriptor(void** buf, uint32_t len);

  template<typename T>
  DescriptorChainBuilder& AppendWritableDescriptor(T** ptr, uint32_t len) {
    return AppendWritableDescriptor(reinterpret_cast<void**>(ptr), len);
  }

  zx_status_t Build();

 private:
  VirtioQueueFake& queue_fake_;
  size_t chain_len_ = 0;
  uint16_t prev_idx_ = 0;
  uint16_t head_idx_ = 0;
  zx_status_t status_ = ZX_OK;

  DescriptorChainBuilder& AppendDescriptor(void** buf, uint32_t len,
                                           uint16_t flags);
};

#endif  // GARNET_BIN_GUEST_VMM_DEVICE_VIRTIO_QUEUE_FAKE_H_
