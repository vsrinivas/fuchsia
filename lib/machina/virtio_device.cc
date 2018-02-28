// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_device.h"

#include <stdio.h>
#include <string.h>

#include <fbl/auto_lock.h>

#include "lib/fxl/logging.h"

#define QUEUE_SIZE 128u

namespace machina {

VirtioDevice::VirtioDevice(uint8_t device_id,
                           void* config,
                           size_t config_size,
                           VirtioQueue* queues,
                           uint16_t num_queues,
                           const PhysMem& phys_mem)
    : device_id_(device_id),
      device_config_(config),
      device_config_size_(config_size),
      queues_(queues),
      num_queues_(num_queues),
      phys_mem_(phys_mem),
      pci_(this) {
  // Virt queue initialization.
  for (int i = 0; i < num_queues_; ++i) {
    queues_[i].set_size(QUEUE_SIZE);
    queues_[i].set_device(this);
  }
}

zx_status_t VirtioDevice::NotifyGuest() {
  bool interrupt = false;
  {
    fbl::AutoLock lock(&mutex_);
    interrupt = isr_status_ > 0;
  }

  if (!interrupt) {
    return ZX_OK;
  }
  return pci_.Interrupt();
}

zx_status_t VirtioDevice::Kick(uint16_t kicked_queue) {
  if (kicked_queue >= num_queues_)
    return ZX_ERR_OUT_OF_RANGE;

  zx_status_t status = HandleQueueNotify(kicked_queue);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to handle queue notify event";
    return status;
  }

  // Send an interrupt back to the guest if we've generated one while
  // processing the queue.
  status = NotifyGuest();
  if (status != ZX_OK) {
    return status;
  }

  // Notify threads waiting on a descriptor.
  fbl::AutoLock lock(&mutex_);
  queues_[kicked_queue].Signal();
  return ZX_OK;
}

zx_status_t VirtioDevice::ReadConfig(uint64_t addr, IoValue* value) {
  fbl::AutoLock lock(&config_mutex_);
  switch (value->access_size) {
    case 1: {
      uint8_t* buf = reinterpret_cast<uint8_t*>(device_config_);
      value->u8 = buf[addr];
      return ZX_OK;
    }
    case 2: {
      uint16_t* buf = reinterpret_cast<uint16_t*>(device_config_);
      value->u16 = buf[addr / 2];
      return ZX_OK;
    }
    case 4: {
      uint32_t* buf = reinterpret_cast<uint32_t*>(device_config_);
      value->u32 = buf[addr / 4];
      return ZX_OK;
    }
  }
  FXL_LOG(ERROR) << "Unsupported config read 0x" << std::hex << addr;
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t VirtioDevice::WriteConfig(uint64_t addr, const IoValue& value) {
  fbl::AutoLock lock(&config_mutex_);
  switch (value.access_size) {
    case 1: {
      uint8_t* buf = reinterpret_cast<uint8_t*>(device_config_);
      buf[addr] = value.u8;
      return ZX_OK;
    }
    case 2: {
      uint16_t* buf = reinterpret_cast<uint16_t*>(device_config_);
      buf[addr / 2] = value.u16;
      return ZX_OK;
    }
    case 4: {
      uint32_t* buf = reinterpret_cast<uint32_t*>(device_config_);
      buf[addr / 4] = value.u32;
      return ZX_OK;
    }
  }
  FXL_LOG(ERROR) << "Unsupported config write 0x" << std::hex << addr;
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace machina
