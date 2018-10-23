// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "broker.h"

#include <stdio.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/protocol/nand.h>
#include <ddktl/device.h>
#include <ddktl/protocol/nand.h>
#include <lib/sync/completion.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <zircon/assert.h>
#include <zircon/device/nand-broker.h>
#include <zircon/types.h>


namespace {

// Wrapper for a nand_op_t.
class Operation {
  public:
    explicit Operation(size_t op_size) {
        raw_buffer_.reset(new uint8_t[op_size]);

        memset(raw_buffer_.get(), 0, op_size);
        nand_op_t* op = reinterpret_cast<nand_op_t*>(raw_buffer_.get());
        op->completion_cb = OnCompletion;
        op->cookie = this;
    }
    ~Operation() {}

    nand_op_t* GetOperation() { return reinterpret_cast<nand_op_t*>(raw_buffer_.get()); }

    // Waits for the operation to complete and returns the operation's status.
    zx_status_t Wait() {
        zx_status_t status = sync_completion_wait(&event_, ZX_TIME_INFINITE);
        sync_completion_reset(&event_);
        return status != ZX_OK ? status : status_;
    }

  private:
    static void OnCompletion(nand_op_t* op, zx_status_t status) {
        Operation* operation = reinterpret_cast<Operation*>(op->cookie);
        operation->status_ = status;
        sync_completion_signal(&operation->event_);
    }

    sync_completion_t event_;
    zx_status_t status_ = ZX_ERR_INTERNAL;
    fbl::unique_ptr<uint8_t[]> raw_buffer_;
};

class Broker;
using DeviceType = ddk::Device<Broker, ddk::Unbindable, ddk::Ioctlable>;

// Exposes a control device (nand-broker) for a nand protocol device.
class Broker : public DeviceType {
  public:
    explicit Broker(zx_device_t* parent) : DeviceType(parent) {}
    ~Broker() {}

    zx_status_t Bind();
    void DdkRelease() { delete this; }

    // Device protocol implementation.
    void DdkUnbind() { DdkRemove(); }
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);

  private:
    zx_status_t Query(zircon_nand_Info* info);
    zx_status_t Queue(uint32_t command, const nand_broker_request_t& request,
                      nand_broker_response_t* response);

    nand_protocol_t nand_protocol_;
    size_t op_size_ = 0;
};

zx_status_t Broker::Bind() {
    if (device_get_protocol(parent(), ZX_PROTOCOL_NAND, &nand_protocol_) != ZX_OK) {
        zxlogf(ERROR, "nand-broker: device '%s' does not support nand protocol\n",
               device_get_name(parent()));
        return ZX_ERR_NOT_SUPPORTED;
    }

    zircon_nand_Info info;
    Query(&info);
    if (!op_size_) {
        zxlogf(ERROR, "nand-broker: unable to query the nand driver\n");
        return ZX_ERR_NOT_SUPPORTED;
    }
    zxlogf(INFO, "nand-broker: %d blocks of %d pages each. Page size: %d\n", info.num_blocks,
           info.pages_per_block, info.page_size);

    return DdkAdd("broker");
}

zx_status_t Broker::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_NAND_BROKER_UNLINK:
        DdkUnbind();
        return ZX_OK;

    case IOCTL_NAND_BROKER_GET_INFO:
        if (out_len < sizeof(zircon_nand_Info)) {
            return ZX_ERR_INVALID_ARGS;
        }
        *out_actual = sizeof(zircon_nand_Info);
        return Query(reinterpret_cast<zircon_nand_Info*>(out_buf));

    case IOCTL_NAND_BROKER_READ:
    case IOCTL_NAND_BROKER_WRITE:
    case IOCTL_NAND_BROKER_ERASE:
        if (in_len != sizeof(nand_broker_request_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        if (out_len < sizeof(nand_broker_response_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        *out_actual = sizeof(nand_broker_response_t);
        return Queue(op, *reinterpret_cast<const nand_broker_request_t*>(in_buf),
                     reinterpret_cast<nand_broker_response_t*>(out_buf));

    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t Broker::Query(zircon_nand_Info* info) {
    ddk::NandProtocolProxy proxy(&nand_protocol_);
    proxy.Query(info, &op_size_);
    return ZX_OK;
}

zx_status_t Broker::Queue(uint32_t command, const nand_broker_request_t& request,
                          nand_broker_response_t* response) {
    Operation operation(op_size_);
    nand_op_t* op = operation.GetOperation();
    *response = {};

    switch (command) {
    case IOCTL_NAND_BROKER_READ:
    case IOCTL_NAND_BROKER_WRITE:
        op->rw.command = (command == IOCTL_NAND_BROKER_READ) ? NAND_OP_READ : NAND_OP_WRITE;
        op->rw.length = request.length;
        op->rw.offset_nand = request.offset_nand;
        op->rw.offset_data_vmo = request.offset_data_vmo;
        op->rw.offset_oob_vmo = request.offset_oob_vmo;
        op->rw.data_vmo = request.data_vmo ? request.vmo : ZX_HANDLE_INVALID;
        op->rw.oob_vmo = request.oob_vmo ? request.vmo : ZX_HANDLE_INVALID;
        break;
    case IOCTL_NAND_BROKER_ERASE:
        op->erase.command = NAND_OP_ERASE;
        op->erase.first_block = request.offset_nand;
        op->erase.num_blocks = request.length;
        break;
    default:
        ZX_DEBUG_ASSERT(false);
    }

    ddk::NandProtocolProxy proxy(&nand_protocol_);
    proxy.Queue(op);

    response->status = operation.Wait();

    if (command == IOCTL_NAND_BROKER_READ) {
        response->corrected_bit_flips = op->rw.corrected_bit_flips;
    }

    if ((command == IOCTL_NAND_BROKER_READ || command == IOCTL_NAND_BROKER_WRITE) &&
        request.vmo != ZX_HANDLE_INVALID) {
        zx_handle_close(request.vmo);
    }

    return ZX_OK;
}

}  // namespace

zx_status_t nand_broker_bind(void* ctx, zx_device_t* parent) {
    zxlogf(INFO, "nand-broker: binding\n");
    fbl::AllocChecker checker;
    fbl::unique_ptr<Broker> device(new (&checker) Broker(parent));
    if (!checker.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = device->Bind();
    if (status == ZX_OK) {
        // devmgr is now in charge of the device.
        __UNUSED Broker* dummy = device.release();
    }
    return status;
}
