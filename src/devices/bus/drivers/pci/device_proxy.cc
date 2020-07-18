// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_proxy.h"

#include <cstring>

#include <ddk/binding.h>
#include <ddk/debug.h>

#include "common.h"

#define RPC_ENTRY zxlogf(TRACE, "[%s] %s: entry", cfg_->addr(), __func__)

#define DEVICE_PROXY_UNIMPLEMENTED                   \
  zxlogf(INFO, "[DeviceProxy] called %s", __func__); \
  return ZX_ERR_NOT_SUPPORTED

// This file contains the PciProtocol implementation that is proxied over
// a channel to the specific pci::Device objects in the PCI Bus Driver.
namespace pci {

zx_status_t DeviceProxy::Create(zx_device_t* parent, zx_handle_t rpcch, const char* name) {
  DeviceProxy* dp = new DeviceProxy(parent, rpcch);
  return dp->DdkAdd(name);
}

zx_status_t DeviceProxy::RpcRequest(PciRpcOp op, zx_handle_t* handle, PciRpcMsg* req,
                                    PciRpcMsg* resp) {
  if (rpcch_ == ZX_HANDLE_INVALID) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint32_t handle_cnt = 0;
  if (handle) {
    // Since only the caller knows if they expected a valid handle back, make
    // sure the handle is invalid if we didn't get one.
    *handle = ZX_HANDLE_INVALID;
    handle_cnt = 1;
  }

  req->op = op;
  zx_channel_call_args_t cc_args = {};
  cc_args.wr_bytes = req;
  cc_args.wr_num_bytes = sizeof(*req);
  cc_args.rd_bytes = resp;
  cc_args.rd_num_bytes = sizeof(*resp);
  cc_args.rd_handles = handle;
  cc_args.rd_num_handles = handle_cnt;
  cc_args.wr_handles = nullptr;
  cc_args.wr_num_handles = 0;

  uint32_t actual_bytes;
  uint32_t actual_handles;
  zx_status_t st =
      rpcch_.call(0, zx::time(ZX_TIME_INFINITE), &cc_args, &actual_bytes, &actual_handles);
  if (st != ZX_OK) {
    return st;
  }

  if (actual_bytes != sizeof(*resp)) {
    return ZX_ERR_INTERNAL;
  }

  return resp->ret;
}

zx_status_t DeviceProxy::DdkGetProtocol(uint32_t proto_id, void* out) {
  if (proto_id == ZX_PROTOCOL_PCI) {
    auto proto = static_cast<pci_protocol_t*>(out);
    proto->ctx = this;
    proto->ops = &pci_protocol_ops_;
    return ZX_OK;
  }

  return ZX_ERR_NOT_SUPPORTED;
}

// TODO(ZX-3927): Convert this to using a better wire format when we no longer
// have to support the kernel driver.
zx_status_t DeviceProxy::PciGetBar(uint32_t bar_id, zx_pci_bar_t* out_bar) {
  PciRpcMsg req = {};
  PciRpcMsg resp = {};
  zx_handle_t handle;

  req.bar.id = bar_id;
  zx_status_t st = RpcRequest(PCI_OP_GET_BAR, &handle, &req, &resp);
  // |st| is the channel operation status, |resp.ret| is the RPC status.
  if (st != ZX_OK) {
    return st;
  }

  if (resp.ret != ZX_OK) {
    return resp.ret;
  }

  out_bar->id = resp.bar.id;
  if (!resp.bar.is_mmio) {
    out_bar->type = ZX_PCI_BAR_TYPE_PIO;
    // TODO(cja): Figure out once and for all what the story is with IO on ARM.
#if __x86_64__
    out_bar->addr = resp.bar.io_addr;
    out_bar->size = resp.bar.io_size;
    // x86 PIO space access requires permission in the I/O bitmap. If an IO BAR
    // is used then the handle returned corresponds to a resource with access to
    // this range of IO space.
    //
    // In a test environment we are not passed a handle back. We can still verify
    // the I/O address and size.
    if (handle != ZX_HANDLE_INVALID) {
      st = zx_ioports_request(handle, static_cast<uint16_t>(out_bar->addr),
                              static_cast<uint32_t>(out_bar->size));
      if (st != ZX_OK) {
        zxlogf(ERROR, "Failed to map IO window for bar into process: %d", st);
        return st;
      }
    }
#else
    zxlogf(INFO,
           "%s: PIO bars may not be supported correctly on this arch. "
           "Please have someone check this!\n",
           __func__);
    return ZX_ERR_NOT_SUPPORTED;
#endif
  } else {
    out_bar->type = ZX_PCI_BAR_TYPE_MMIO;
    out_bar->handle = handle;
  }

  return ZX_OK;
}

zx_status_t DeviceProxy::PciEnableBusMaster(bool enable) {
  PciRpcMsg req = {};
  PciRpcMsg resp = {};

  req.enable = enable;
  return RpcRequest(PCI_OP_ENABLE_BUS_MASTER, nullptr, &req, &resp);
}

zx_status_t DeviceProxy::PciResetDevice() { DEVICE_PROXY_UNIMPLEMENTED; }

zx_status_t DeviceProxy::PciMapInterrupt(uint32_t which_irq, zx::interrupt* out_handle) {
  PciRpcMsg req = {};
  PciRpcMsg resp = {};

  req.irq.which_irq = which_irq;
  zx_handle_t irq_handle;
  zx_status_t st = RpcRequest(PCI_OP_MAP_INTERRUPT, &irq_handle, &req, &resp);
  if (st == ZX_OK) {
    out_handle->reset(irq_handle);
  }

  return st;
}

zx_status_t DeviceProxy::PciConfigureIrqMode(uint32_t requested_irq_count) {
  PciRpcMsg req = {};
  PciRpcMsg resp = {};

  req.irq.requested_irqs = requested_irq_count;
  return RpcRequest(PCI_OP_CONFIGURE_IRQ_MODE, nullptr, &req, &resp);
}

zx_status_t DeviceProxy::PciQueryIrqMode(zx_pci_irq_mode_t mode, uint32_t* out_max_irqs) {
  PciRpcMsg req = {};
  PciRpcMsg resp = {};

  req.irq.mode = mode;
  resp.irq.mode = mode;
  zx_status_t st = RpcRequest(PCI_OP_QUERY_IRQ_MODE, nullptr, &req, &resp);
  if (st == ZX_OK) {
    *out_max_irqs = resp.irq.max_irqs;
  }
  return st;
}

zx_status_t DeviceProxy::PciSetIrqMode(zx_pci_irq_mode_t mode, uint32_t requested_irq_count) {
  PciRpcMsg req = {};
  PciRpcMsg resp = {};

  req.irq.mode = mode;
  req.irq.requested_irqs = requested_irq_count;
  return RpcRequest(PCI_OP_SET_IRQ_MODE, nullptr, &req, &resp);
}

zx_status_t DeviceProxy::PciGetDeviceInfo(zx_pcie_device_info_t* out_info) {
  PciRpcMsg req = {};
  PciRpcMsg resp = {};

  zx_status_t st = RpcRequest(PCI_OP_GET_DEVICE_INFO, nullptr, &req, &resp);
  if (st == ZX_OK) {
    *out_info = resp.info;
  }
  return st;
}

template <typename T>
zx_status_t DeviceProxy::PciConfigRead(uint16_t offset, T* out_value) {
  PciRpcMsg req = {};
  PciRpcMsg resp = {};

  req.cfg.offset = offset;
  req.cfg.width = static_cast<uint16_t>(sizeof(T));
  zx_status_t st = RpcRequest(PCI_OP_CONFIG_READ, nullptr, &req, &resp);
  if (st == ZX_OK) {
    *out_value = static_cast<T>(resp.cfg.value);
  }
  return st;
}

zx_status_t DeviceProxy::PciConfigRead8(uint16_t offset, uint8_t* out_value) {
  return PciConfigRead(offset, out_value);
}

zx_status_t DeviceProxy::PciConfigRead16(uint16_t offset, uint16_t* out_value) {
  return PciConfigRead(offset, out_value);
}

zx_status_t DeviceProxy::PciConfigRead32(uint16_t offset, uint32_t* out_value) {
  return PciConfigRead(offset, out_value);
}

template <typename T>
zx_status_t DeviceProxy::PciConfigWrite(uint16_t offset, T value) {
  PciRpcMsg req = {};
  PciRpcMsg resp = {};

  req.cfg.offset = offset;
  req.cfg.width = static_cast<uint16_t>(sizeof(T));
  req.cfg.value = value;
  return RpcRequest(PCI_OP_CONFIG_WRITE, nullptr, &req, &resp);
}

zx_status_t DeviceProxy::PciConfigWrite8(uint16_t offset, uint8_t value) {
  return PciConfigWrite(offset, value);
}

zx_status_t DeviceProxy::PciConfigWrite16(uint16_t offset, uint16_t value) {
  return PciConfigWrite(offset, value);
}

zx_status_t DeviceProxy::PciConfigWrite32(uint16_t offset, uint32_t value) {
  return PciConfigWrite(offset, value);
}

zx_status_t DeviceProxy::PciGetFirstCapability(uint8_t cap_id, uint8_t* out_offset) {
  return PciGetNextCapability(cap_id, kPciCapOffsetFirst, out_offset);
}

zx_status_t DeviceProxy::PciGetNextCapability(uint8_t cap_id, uint8_t offset, uint8_t* out_offset) {
  if (!out_offset) {
    return ZX_ERR_INVALID_ARGS;
  }

  PciRpcMsg req;
  memset(&req, 0, sizeof(req));
  req.cap.id = cap_id;
  if (offset == kPciCapOffsetFirst) {
    req.cap.is_first = true;
    req.cap.offset = 0;
  } else {
    req.cap.offset = offset;
  }

  PciRpcMsg resp;
  memset(&resp, 0, sizeof(resp));
  zx_status_t st = RpcRequest(PCI_OP_GET_NEXT_CAPABILITY, nullptr, &req, &resp);
  if (st == ZX_OK) {
    *out_offset = static_cast<uint8_t>(resp.cap.offset);
  }
  return st;
}

zx_status_t DeviceProxy::PciGetFirstExtendedCapability(uint16_t cap_id, uint16_t* out_offset) {
  return PciGetNextExtendedCapability(cap_id, kPciExtCapOffsetFirst, out_offset);
}

zx_status_t DeviceProxy::PciGetNextExtendedCapability(uint16_t cap_id, uint16_t offset,
                                                      uint16_t* out_offset) {
  if (!out_offset) {
    return ZX_ERR_INVALID_ARGS;
  }

  PciRpcMsg req;
  memset(&req, 0, sizeof(req));
  req.cap.id = cap_id;
  if (offset == kPciExtCapOffsetFirst) {
    req.cap.is_first = true;
    req.cap.offset = 0;
  } else {
    req.cap.offset = offset;
  }
  req.cap.is_extended = true;

  PciRpcMsg resp;
  memset(&resp, 0, sizeof(resp));
  zx_status_t st = RpcRequest(PCI_OP_GET_NEXT_CAPABILITY, nullptr, &req, &resp);
  if (st == ZX_OK) {
    *out_offset = resp.cap.offset;
  }
  return st;
}

// TODO(ZX-3146): These methods need to be deleted, or refactored.
zx_status_t DeviceProxy::PciGetAuxdata(const char* args, void* out_data_buffer, size_t data_size,
                                       size_t* out_data_actual) {
  DEVICE_PROXY_UNIMPLEMENTED;
}

zx_status_t DeviceProxy::PciGetBti(uint32_t index, zx::bti* out_bti) { DEVICE_PROXY_UNIMPLEMENTED; }

}  // namespace pci

static zx_status_t pci_device_proxy_create(void* ctx, zx_device_t* parent, const char* name,
                                           const char* args, zx_handle_t rpcch) {
  return pci::DeviceProxy::Create(parent, rpcch, name);
}

static constexpr zx_driver_ops_t pci_device_proxy_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.create = pci_device_proxy_create;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(pci_device_proxy, pci_device_proxy_driver_ops, "zircon", "0.1", 1)
    BI_ABORT_IF_AUTOBIND,
ZIRCON_DRIVER_END(pci_device_proxy)
