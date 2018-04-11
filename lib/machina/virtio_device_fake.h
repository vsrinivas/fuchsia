// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_DEVICE_FAKE_H_
#define GARNET_LIB_MACHINA_VIRTIO_DEVICE_FAKE_H_

#include "garnet/lib/machina/phys_mem_fake.h"
#include "garnet/lib/machina/virtio_device.h"
#include "garnet/lib/machina/virtio_queue_fake.h"
#include "gtest/gtest.h"

#define QUEUE_SIZE 16
#define VIRTIO_TEST_ID 30

namespace machina {

typedef struct test_config {
} test_config_t;

class VirtioDeviceFake
    : public VirtioDeviceBase<VIRTIO_TEST_ID, 1, test_config_t> {
 public:
  VirtioDeviceFake() : VirtioDeviceBase(phys_mem_), queue_fake_(queue()) {}

  zx_status_t Init() { return queue_fake_.Init(QUEUE_SIZE); }

  VirtioQueue* queue() { return VirtioDeviceBase::queue(0); }
  VirtioQueueFake& queue_fake() { return queue_fake_; }

 private:
  PhysMemFake phys_mem_;
  VirtioQueueFake queue_fake_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_DEVICE_FAKE_H_
