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
#include <fuchsia/nand/c/fidl.h>
#include <zircon/assert.h>
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
using DeviceType = ddk::Device<Broker, ddk::Unbindable, ddk::Messageable>;

// Exposes a control device (nand-broker) for a nand protocol device.
class Broker : public DeviceType {
  public:
    explicit Broker(zx_device_t* parent) : DeviceType(parent) {}
    ~Broker() {}

    zx_status_t Bind();
    void DdkRelease() { delete this; }

    // Device protocol implementation.
    void DdkUnbind() { DdkRemove(); }
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

    // fidl interface.
    zx_status_t GetInfo(zircon_nand_Info* info) {
        return Query(info);
    }
    zx_status_t Read(const fuchsia_nand_BrokerRequest& request, uint32_t* corrected_bits) {
        return Queue(NAND_OP_READ, request, corrected_bits);
    }
    zx_status_t Write(const fuchsia_nand_BrokerRequest& request) {
        return Queue(NAND_OP_WRITE, request, nullptr);
    }
    zx_status_t Erase(const fuchsia_nand_BrokerRequest& request) {
        return Queue(NAND_OP_ERASE, request, nullptr);
    }

  private:
    zx_status_t Query(zircon_nand_Info* info);
    zx_status_t Queue(uint32_t command, const fuchsia_nand_BrokerRequest& request,
                      uint32_t* corrected_bits);

    nand_protocol_t nand_protocol_;
    size_t op_size_ = 0;
};

zx_status_t GetInfo(void* ctx, fidl_txn_t* txn)  {
    Broker* device = reinterpret_cast<Broker*>(ctx);
    zircon_nand_Info info;
    zx_status_t status = device->GetInfo(&info);
    return fuchsia_nand_BrokerGetInfo_reply(txn, status, &info);
}

zx_status_t Read(void* ctx, const fuchsia_nand_BrokerRequest* request, fidl_txn_t* txn)  {
    Broker* device = reinterpret_cast<Broker*>(ctx);
    uint32_t corrected_bits = 0;
    zx_status_t status = device->Read(*request, &corrected_bits);
    return fuchsia_nand_BrokerRead_reply(txn, status, corrected_bits);
}

zx_status_t Write(void* ctx, const fuchsia_nand_BrokerRequest* request, fidl_txn_t* txn)  {
    Broker* device = reinterpret_cast<Broker*>(ctx);
    zx_status_t status = device->Write(*request);
    return fuchsia_nand_BrokerWrite_reply(txn, status);
}

zx_status_t Erase(void* ctx, const fuchsia_nand_BrokerRequest* request, fidl_txn_t* txn)  {
    Broker* device = reinterpret_cast<Broker*>(ctx);
    zx_status_t status = device->Erase(*request);
    return fuchsia_nand_BrokerErase_reply(txn, status);
}

fuchsia_nand_Broker_ops_t fidl_ops = {
    .GetInfo = GetInfo,
    .Read = Read,
    .Write = Write,
    .Erase = Erase
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

zx_status_t Broker::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_nand_Broker_dispatch(this, txn, msg, &fidl_ops);
}

zx_status_t Broker::Query(zircon_nand_Info* info) {
    ddk::NandProtocolProxy proxy(&nand_protocol_);
    proxy.Query(info, &op_size_);
    return ZX_OK;
}

zx_status_t Broker::Queue(uint32_t command, const fuchsia_nand_BrokerRequest& request,
                          uint32_t* corrected_bits) {
    Operation operation(op_size_);
    nand_op_t* op = operation.GetOperation();
    op->rw.command = command;

    switch (command) {
    case NAND_OP_READ:
    case NAND_OP_WRITE:
        op->rw.length = request.length;
        op->rw.offset_nand = request.offset_nand;
        op->rw.offset_data_vmo = request.offset_data_vmo;
        op->rw.offset_oob_vmo = request.offset_oob_vmo;
        op->rw.data_vmo = request.data_vmo ? request.vmo : ZX_HANDLE_INVALID;
        op->rw.oob_vmo = request.oob_vmo ? request.vmo : ZX_HANDLE_INVALID;
        break;
    case NAND_OP_ERASE:
        op->erase.first_block = request.offset_nand;
        op->erase.num_blocks = request.length;
        break;
    default:
        ZX_DEBUG_ASSERT(false);
    }

    ddk::NandProtocolProxy proxy(&nand_protocol_);
    proxy.Queue(op);

    zx_status_t status = operation.Wait();

    if (command == NAND_OP_READ) {
        *corrected_bits = op->rw.corrected_bit_flips;
    }

    if ((command == NAND_OP_READ || command == NAND_OP_WRITE) &&
        request.vmo != ZX_HANDLE_INVALID) {
        zx_handle_close(request.vmo);
    }

    return status;
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
