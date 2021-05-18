// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/nand/c/banjo.h>
#include <fuchsia/hardware/nand/cpp/banjo.h>
#include <fuchsia/nand/llcpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/sync/completion.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <memory>

#include <ddktl/device.h>
#include <fbl/alloc_checker.h>

#include "src/devices/lib/nand/nand.h"
#include "src/devices/nand/drivers/broker/nand-broker-bind.h"

namespace {

// Wrapper for a nand_operation_t.
class Operation {
 public:
  explicit Operation(size_t op_size) {
    raw_buffer_.reset(new uint8_t[op_size]);

    memset(raw_buffer_.get(), 0, op_size);
  }
  ~Operation() {}

  nand_operation_t* GetOperation() {
    return reinterpret_cast<nand_operation_t*>(raw_buffer_.get());
  }

  // Waits for the operation to complete and returns the operation's status.
  zx_status_t Submit(ddk::NandProtocolClient& proxy) {
    proxy.Queue(GetOperation(), OnCompletion, this);

    zx_status_t status = sync_completion_wait(&event_, ZX_TIME_INFINITE);
    sync_completion_reset(&event_);
    return status != ZX_OK ? status : status_;
  }

 private:
  static void OnCompletion(void* cookie, zx_status_t status, nand_operation_t* op) {
    Operation* operation = reinterpret_cast<Operation*>(cookie);
    operation->status_ = status;
    sync_completion_signal(&operation->event_);
  }

  sync_completion_t event_;
  zx_status_t status_ = ZX_ERR_INTERNAL;
  std::unique_ptr<uint8_t[]> raw_buffer_;
};

class Broker;
using DeviceType =
    ddk::Device<Broker, ddk::Unbindable, ddk::Messageable<fuchsia_nand::Broker>::Mixin>;

// Exposes a control device (nand-broker) for a nand protocol device.
class Broker : public DeviceType {
 public:
  explicit Broker(zx_device_t* parent) : DeviceType(parent), nand_(parent) {}
  ~Broker() {}

  zx_status_t Bind();
  void DdkRelease() { delete this; }

  // Device protocol implementation.
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

  // fidl interface.
  void GetInfo(GetInfoRequestView request, GetInfoCompleter::Sync& completer) {
    fidl::FidlAllocator allocator;
    fidl::ObjectView<fuchsia_hardware_nand::wire::Info> info(allocator);
    completer.Reply(Query(info.get()), info);
  }
  void Read(ReadRequestView request, ReadCompleter::Sync& completer) {
    uint32_t corrected_bits;
    completer.Reply(Queue(NAND_OP_READ, request->request, &corrected_bits), corrected_bits);
  }
  void Write(WriteRequestView request, WriteCompleter::Sync& completer) {
    completer.Reply(Queue(NAND_OP_WRITE, request->request, nullptr));
  }
  void Erase(EraseRequestView request, EraseCompleter::Sync& completer) {
    completer.Reply(Queue(NAND_OP_ERASE, request->request, nullptr));
  }

 private:
  zx_status_t Query(fuchsia_hardware_nand::wire::Info* info);
  zx_status_t Queue(uint32_t command, fuchsia_nand::wire::BrokerRequest& request,
                    uint32_t* corrected_bits);

  ddk::NandProtocolClient nand_;
  size_t op_size_ = 0;
};

zx_status_t Broker::Bind() {
  if (!nand_.is_valid()) {
    zxlogf(ERROR, "nand-broker: device '%s' does not support nand protocol",
           device_get_name(parent()));
    return ZX_ERR_NOT_SUPPORTED;
  }

  fuchsia_hardware_nand::wire::Info info;
  Query(&info);
  if (!op_size_) {
    zxlogf(ERROR, "nand-broker: unable to query the nand driver");
    return ZX_ERR_NOT_SUPPORTED;
  }
  zxlogf(INFO, "nand-broker: %d blocks of %d pages each. Page size: %d", info.num_blocks,
         info.pages_per_block, info.page_size);

  return DdkAdd("broker");
}

zx_status_t Broker::Query(fuchsia_hardware_nand::wire::Info* info) {
  nand_info_t temp_info;
  nand_.Query(&temp_info, &op_size_);
  nand::nand_fidl_from_banjo(temp_info, info);
  return ZX_OK;
}

zx_status_t Broker::Queue(uint32_t command, fuchsia_nand::wire::BrokerRequest& request,
                          uint32_t* corrected_bits) {
  Operation operation(op_size_);
  nand_operation_t* op = operation.GetOperation();
  op->rw.command = command;

  switch (command) {
    case NAND_OP_READ:
    case NAND_OP_WRITE:
      op->rw.length = request.length;
      op->rw.offset_nand = request.offset_nand;
      op->rw.offset_data_vmo = request.offset_data_vmo;
      op->rw.offset_oob_vmo = request.offset_oob_vmo;
      op->rw.data_vmo = request.data_vmo ? request.vmo.get() : ZX_HANDLE_INVALID;
      op->rw.oob_vmo = request.oob_vmo ? request.vmo.get() : ZX_HANDLE_INVALID;
      break;
    case NAND_OP_ERASE:
      op->erase.first_block = request.offset_nand;
      op->erase.num_blocks = request.length;
      break;
    default:
      ZX_DEBUG_ASSERT(false);
  }

  zx_status_t status = operation.Submit(nand_);

  if (command == NAND_OP_READ) {
    *corrected_bits = op->rw.corrected_bit_flips;
  }

  return status;
}

zx_status_t NandBrokerBind(void* ctx, zx_device_t* parent) {
  zxlogf(INFO, "nand-broker: binding");
  fbl::AllocChecker checker;
  std::unique_ptr<Broker> device(new (&checker) Broker(parent));
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

static constexpr zx_driver_ops_t nand_broker_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = NandBrokerBind;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(nand_broker, nand_broker_ops, "zircon", "0.1");
