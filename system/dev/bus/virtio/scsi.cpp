// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scsi.h"

#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <inttypes.h>
#include <pretty/hexdump.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <virtio/scsi.h>
#include <zircon/compiler.h>

#include <utility>

#include "scsilib.h"
#include "trace.h"

#define LOCAL_TRACE 0

namespace virtio {

// Fill in req->lun with a single-level LUN structure representing target:lun.
void ScsiDevice::FillLUNStructure(struct virtio_scsi_req_cmd* req, uint8_t target, uint16_t lun) {
    req->lun[0] = 1;
    req->lun[1] = target;
    req->lun[2] = 0x40 | static_cast<uint8_t>(lun >> 8);
    req->lun[3] = static_cast<uint8_t>(lun) & 0xff;
}

zx_status_t ScsiDevice::ExecuteCommandSync(uint8_t target, uint16_t lun, uint8_t* cdb,
                                           size_t cdb_length) {
    uint8_t* const request_buffers_addr =
        reinterpret_cast<uint8_t*>(io_buffer_virt(&request_buffers_));
    auto* const req = reinterpret_cast<struct virtio_scsi_req_cmd*>(request_buffers_addr);
    auto* const resp = reinterpret_cast<struct virtio_scsi_resp_cmd*>(
        request_buffers_addr + sizeof(struct virtio_scsi_req_cmd));

    memset(req, 0, sizeof(*req));
    memcpy(&req->cdb, cdb, cdb_length);
    FillLUNStructure(req, /*target=*/target, /*lun=*/lun);

    // virtio-scsi requests have a 'request' region, a data-out region, a
    // 'response' region, and a data-in region. Allocate and fill them
    // and then execute the request.
    //
    // TODO: Currently only allocates two regions, request/response.
    // Add more so we can support most SCSI commands.
    uint16_t id = 0;
    auto request_desc = request_queue_.AllocDescChain(/*count=*/2, &id);
    request_desc->addr = io_buffer_phys(&request_buffers_);
    request_desc->len = sizeof(*req);
    request_desc->flags = VRING_DESC_F_NEXT;

    auto response_desc = request_queue_.DescFromIndex(request_desc->next);
    response_desc->addr = io_buffer_phys(&request_buffers_) + sizeof(*req);
    response_desc->len = sizeof(*resp);
    response_desc->flags = VRING_DESC_F_WRITE;

    request_queue_.SubmitChain(id);
    request_queue_.Kick();

    // Wait for request to complete.
    sync_completion_t sync;
    // annotalysis is unable to determine that ScsiDevice::lock_ is held when the IrqRingUpdate
    // lambda is invoked.
    request_queue_.IrqRingUpdate([this, &sync](vring_used_elem* elem) TA_NO_THREAD_SAFETY_ANALYSIS {
        auto index = static_cast<uint16_t>(elem->id);

        // Synchronously reclaim the entire descriptor chain.
        for (;;) {
            vring_desc const* desc = request_queue_.DescFromIndex(index);
            const bool has_next = desc->flags & VRING_DESC_F_NEXT;
            const uint16_t next = desc->next;

            this->request_queue_.FreeDesc(index);
            if (!has_next) {
                break;
            }
            index = next;
        }
        sync_completion_signal(&sync);
    });
    sync_completion_wait(&sync, ZX_TIME_INFINITE);

    // If there was either a transport or SCSI level error, return a failure.
    if (resp->response || resp->status) {
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

zx_status_t ScsiDevice::WorkerThread() {
    fbl::AutoLock lock(&lock_);

    // Execute TEST UNIT READY on every possible target to find potential disks.
    // TODO(ZX-2314): Move probe sequence to ScsiLib -- have it call down into LLDs to execute
    // commands.
    for (auto channel = 0u; channel < config_.max_channel; channel++) {
        for (uint8_t target = 0u; target < config_.max_target; target++) {
            for (uint16_t lun = 0u; lun < config_.max_lun; lun++) {
                scsi::TestUnitReadyCDB cdb = {};
                cdb.opcode = scsi::Opcode::TEST_UNIT_READY;

                auto status = ExecuteCommandSync(
                    /*target=*/target,
                    /*lun=*/lun, reinterpret_cast<uint8_t*>(&cdb), sizeof(cdb));
                if (status == ZX_OK) {
                    scsi::Disk::Create(device_, /*target=*/target, /*lun=*/lun);
                }
            }
        }
    }

    return ZX_OK;
}

zx_status_t ScsiDevice::Init() {
    LTRACE_ENTRY;

    Device::DeviceReset();
    Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, num_queues),
                                       &config_.num_queues);
    Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, seg_max),
                                       &config_.seg_max);
    Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, max_sectors),
                                       &config_.max_sectors);
    Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, cmd_per_lun),
                                       &config_.cmd_per_lun);
    Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, event_info_size),
                                       &config_.event_info_size);
    Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, sense_size),
                                       &config_.sense_size);
    Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, cdb_size),
                                       &config_.cdb_size);
    Device::ReadDeviceConfig<uint16_t>(offsetof(virtio_scsi_config, max_channel),
                                       &config_.max_channel);
    Device::ReadDeviceConfig<uint16_t>(offsetof(virtio_scsi_config, max_target),
                                       &config_.max_target);
    Device::ReadDeviceConfig<uint32_t>(offsetof(virtio_scsi_config, max_lun),
                                       &config_.max_lun);

    Device::DriverStatusAck();

    {
        fbl::AutoLock lock(&lock_);
        auto err = control_ring_.Init(/*index=*/Queue::CONTROL);
        if (err) {
            zxlogf(ERROR, "failed to allocate control queue\n");
            return err;
        }

        err = request_queue_.Init(/*index=*/Queue::REQUEST);
        if (err) {
            zxlogf(ERROR, "failed to allocate request queue\n");
            return err;
        }

        // Allocate one virtio_scsi_req_cmd / virtio_scsi_resp_cmd per request
        // queue entry.
        const size_t request_buffers_size =
            Device::GetRingSize(Queue::REQUEST) *
            (sizeof(struct virtio_scsi_req_cmd) + sizeof(struct virtio_scsi_resp_cmd));
        auto status =
            io_buffer_init(&request_buffers_, bti().get(),
                           /*size=*/request_buffers_size, IO_BUFFER_RW | IO_BUFFER_CONTIG);
        if (status) {
            zxlogf(ERROR, "failed to allocate queue working memory\n");
            return status;
        }
    }

    Device::StartIrqThread();
    Device::DriverStatusOk();

    device_add_args_t args{};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "virtio-scsi";
    args.ops = &device_ops_;
    args.ctx = this;

    // Synchronize against Unbind()/Release() before the worker thread is running.
    fbl::AutoLock lock(&lock_);
    auto status = device_add(Device::bus_device_, &args, &device_);
    if (status != ZX_OK) {
        return status;
    }

    auto td = [](void* ctx) {
        ScsiDevice* const device = static_cast<ScsiDevice*>(ctx);
        return device->WorkerThread();
    };
    int ret = thrd_create_with_name(&worker_thread_, td, this, "virtio-scsi-worker");
    if (ret != thrd_success) {
        return ZX_ERR_INTERNAL;
    }

    return status;
}

void ScsiDevice::Unbind() {
    Device::Unbind();
}

void ScsiDevice::Release() {
    {
        fbl::AutoLock lock(&lock_);
        worker_thread_should_exit_ = true;
    }
    thrd_join(worker_thread_, nullptr);
    Device::Release();
}

} // namespace virtio
