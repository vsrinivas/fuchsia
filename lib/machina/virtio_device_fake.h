// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/phys_mem_fake.h"
#include "garnet/lib/machina/virtio_device.h"
#include "garnet/lib/machina/virtio_queue_fake.h"
#include "gtest/gtest.h"

#define QUEUE_SIZE 16
#define VIRTIO_TEST_ID 30

namespace machina {

class VirtioDeviceFake : public VirtioDevice {
 public:
  VirtioDeviceFake()
      : VirtioDevice(VIRTIO_TEST_ID, nullptr, 0, &queue_, 1, phys_mem_),
        queue_fake_(&queue_) {}

  zx_status_t Init() { return queue_fake_.Init(QUEUE_SIZE); }

  VirtioQueue& queue() { return queue_; }
  VirtioQueueFake& queue_fake() { return queue_fake_; }

 private:
  VirtioQueue queue_;
  PhysMemFake phys_mem_;
  VirtioQueueFake queue_fake_;
};

}  // namespace machina
