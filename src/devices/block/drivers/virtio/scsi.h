// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_BUS_VIRTIO_SCSI_H_
#define ZIRCON_SYSTEM_DEV_BUS_VIRTIO_SCSI_H_

#include <lib/scsi/scsilib_controller.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <zircon/compiler.h>

#include <atomic>
#include <memory>

#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
#include <virtio/scsi.h>

#include "src/devices/bus/lib/virtio/backends/backend.h"
#include "src/devices/bus/lib/virtio/device.h"
#include "src/devices/bus/lib/virtio/ring.h"

namespace virtio {

constexpr int MAX_IOS = 16;

class ScsiDevice : public virtio::Device,
                   public scsi::Controller,
                   public ddk::Device<ScsiDevice, ddk::UnbindableDeprecated> {
 public:
  enum Queue {
    CONTROL = 0,
    EVENT = 1,
    REQUEST = 2,
  };

  ScsiDevice(zx_device_t* device, zx::bti bti, std::unique_ptr<Backend> backend)
      : virtio::Device(device, std::move(bti), std::move(backend)),
        ddk::Device<ScsiDevice, ddk::UnbindableDeprecated>(device) {}

  // virtio::Device overrides
  zx_status_t Init() override;
  void DdkUnbindDeprecated();
  void DdkRelease();
  // Invoked for most device interrupts.
  virtual void IrqRingUpdate() override;
  // Invoked on config change interrupts.
  void IrqConfigChange() override {}

  const char* tag() const override { return "virtio-scsi"; }

  static void FillLUNStructure(struct virtio_scsi_req_cmd* req, uint8_t target, uint16_t lun);

 private:
  zx_status_t TargetMaxXferSize(uint8_t target, uint16_t lun, uint32_t& xfer_size_sectors);

  zx_status_t ExecuteCommandSync(uint8_t target, uint16_t lun, struct iovec cdb,
                                 struct iovec data_out, struct iovec data_in) override;

  zx_status_t ExecuteCommandAsync(uint8_t target, uint16_t lun, struct iovec cdb,
                                  struct iovec data_out, struct iovec data_in,
                                  void (*cb)(void*, zx_status_t), void* cookie) override;
  zx_status_t WorkerThread();

  // Latched copy of virtio-scsi device configuration.
  struct virtio_scsi_config config_ TA_GUARDED(lock_) = {};

  struct scsi_io_slot {
    io_buffer_t request_buffer;
    bool avail;
    vring_desc* tail_desc;
    void* cookie;
    void (*callback)(void* cookie, zx_status_t status);
    struct iovec data_in;
    void* data_in_region;
    io_buffer_t* request_buffers;
    struct virtio_scsi_resp_cmd* response;
  };
  scsi_io_slot* GetIO() TA_REQ(lock_);
  void FreeIO(scsi_io_slot* io_slot) TA_REQ(lock_);
  size_t request_buffers_size_;
  scsi_io_slot scsi_io_slot_table_[MAX_IOS] TA_GUARDED(lock_) = {};

  Ring control_ring_ TA_GUARDED(lock_) = {this};
  Ring request_queue_ = {this};

  thrd_t worker_thread_;
  bool worker_thread_should_exit_ TA_GUARDED(lock_) = {};

  // Synchronizes virtio rings and worker thread control.
  fbl::Mutex lock_;

  // We use the condvar to control the number of IO's in flight
  // as well as to wait for descs to become available.
  fbl::ConditionVariable ioslot_cv_ __TA_GUARDED(lock_);
  fbl::ConditionVariable desc_cv_ __TA_GUARDED(lock_);
  uint32_t active_ios_ __TA_GUARDED(lock_);
  uint64_t scsi_transport_tag_ __TA_GUARDED(lock_);
};

}  // namespace virtio

#endif  // ZIRCON_SYSTEM_DEV_BUS_VIRTIO_SCSI_H_
