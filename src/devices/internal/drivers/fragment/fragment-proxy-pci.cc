// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/zx/status.h>

#include "src/devices/bus/drivers/pci/proxy_rpc.h"
#include "src/devices/internal/drivers/fragment/fragment-proxy.h"
#include "src/devices/internal/drivers/fragment/proxy-protocol.h"

namespace fragment {

zx_status_t FragmentProxy::PciRpc(pci::PciRpcOp op, zx_handle_t* rd_handle,
                                  const zx_handle_t* wr_handle, PciRpcRequest* req,
                                  PciRpcResponse* resp) {
  ZX_DEBUG_ASSERT(req != nullptr);
  ZX_DEBUG_ASSERT(resp != nullptr);
  if (rpc_ == ZX_HANDLE_INVALID) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint32_t rd_handle_cnt = 0;
  if (rd_handle) {
    // Since only the caller knows if they expected a valid handle back, make
    // sure the handle is invalid if we didn't get one.
    *rd_handle = ZX_HANDLE_INVALID;
    rd_handle_cnt = 1;
  }

  uint32_t wr_handle_cnt = 0;
  if (wr_handle && *wr_handle != ZX_HANDLE_INVALID) {
    wr_handle_cnt = 1;
  }

  req->header.proto_id = ZX_PROTOCOL_PCI;
  req->op = op;
  zx_channel_call_args_t cc_args{};
  cc_args.wr_bytes = req;
  cc_args.wr_num_bytes = sizeof(*req);
  cc_args.rd_bytes = resp;
  cc_args.rd_num_bytes = sizeof(*resp);
  cc_args.rd_handles = rd_handle;
  cc_args.rd_num_handles = rd_handle_cnt;
  cc_args.wr_handles = wr_handle;
  cc_args.wr_num_handles = wr_handle_cnt;

  uint32_t actual_bytes;
  uint32_t actual_handles;
  zx_status_t st =
      rpc_.call(0, zx::time(ZX_TIME_INFINITE), &cc_args, &actual_bytes, &actual_handles);
  if (st != ZX_OK) {
    zxlogf(ERROR, "rpc call failed: %s", zx_status_get_string(st));
    return st;
  }

  if (actual_bytes != sizeof(*resp)) {
    zxlogf(ERROR, "rpc payload mismatch (expected: %#lx, actual %#x)", sizeof(*resp), actual_bytes);
    return ZX_ERR_INTERNAL;
  }

  return resp->header.status;
}

// TODO(fxbug.dev/33713): Convert this to using a better wire format when we no longer
// have to support the kernel driver.
zx_status_t FragmentProxy::PciGetBar(uint32_t bar_id, pci_bar_t* out_bar) {
  PciRpcRequest req{};
  PciRpcResponse resp{};
  zx_handle_t handle;

  req.bar.id = bar_id;
  zx_status_t st =
      PciRpc(pci::PCI_OP_GET_BAR, /*rd_handle=*/&handle, /*wr_handle=*/nullptr, &req, &resp);
  // |st| is the channel operation status, |resp.ret| is the RPC status.
  if (st != ZX_OK) {
    return st;
  }

  if (resp.header.status != ZX_OK) {
    return resp.header.status;
  }

  out_bar->bar_id = resp.bar.id;
  out_bar->size = resp.bar.size;
  out_bar->type = (resp.bar.is_mmio) ? PCI_BAR_TYPE_MMIO : PCI_BAR_TYPE_IO;
  if (resp.bar.is_mmio) {
    out_bar->result.vmo = handle;
  } else {
    // x86 PIO space access requires permission in the I/O bitmap.  If an IO BAR
    // is used then the handle returned corresponds to a resource with access to
    // this range of IO space.  On other platforms, like ARM, IO bars are still
    // handled in MMIO space so this type will be unused.
    out_bar->result.io.address = resp.bar.address;
    st = zx_ioports_request(handle, static_cast<uint16_t>(resp.bar.address),
                            static_cast<uint32_t>(resp.bar.size));
    if (st != ZX_OK) {
      zxlogf(ERROR, "Failed to map IO window %#lx for bar into process: %d", resp.bar.address, st);
      return st;
    }
    out_bar->result.io.resource = handle;
  }

  return ZX_OK;
}

zx_status_t FragmentProxy::PciSetBusMastering(bool enable) {
  PciRpcRequest req{};
  PciRpcResponse resp{};

  req.enable = enable;
  return PciRpc(pci::PCI_OP_ENABLE_BUS_MASTER, /*rd_handle=*/nullptr, /*wr_handle=*/nullptr, &req,
                &resp);
}

zx_status_t FragmentProxy::PciResetDevice() { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t FragmentProxy::PciAckInterrupt() {
#ifdef ENABLE_USER_PCI
  PciRpcRequest req{};
  PciRpcResponse resp{};
  return PciRpc(pci::PCI_OP_ACK_INTERRUPT, /*rd_handle=*/nullptr, /*wr_handle=*/nullptr,
                /*req=*/&req, /*resp=*/&resp);
#else
  return ZX_OK;
#endif
}

zx_status_t FragmentProxy::PciMapInterrupt(uint32_t which_irq, zx::interrupt* out_handle) {
  PciRpcRequest req{};
  PciRpcResponse resp{};

  req.irq.which_irq = which_irq;
  zx_handle_t irq_handle;
  zx_status_t st = PciRpc(pci::PCI_OP_MAP_INTERRUPT, /*rd_handle=*/&irq_handle,
                          /*wr_handle=*/nullptr, &req, &resp);
  if (st == ZX_OK) {
    out_handle->reset(irq_handle);
  }

  return st;
}

void FragmentProxy::PciGetInterruptModes(pci_interrupt_modes_t* out_modes) {
  PciRpcRequest req{};
  PciRpcResponse resp{};
  zx_status_t st = PciRpc(pci::PCI_OP_GET_INTERRUPT_MODES, /*rd_handle=*/nullptr,
                          /*wr_handle=*/nullptr, &req, &resp);
  if (st == ZX_OK) {
    *out_modes = resp.irq.modes;
  }
}

zx_status_t FragmentProxy::PciSetInterruptMode(pci_interrupt_mode_t mode,
                                               uint32_t requested_irq_count) {
  PciRpcRequest req{};
  PciRpcResponse resp{};

  req.irq.mode = mode;
  req.irq.requested_irqs = requested_irq_count;
  return PciRpc(pci::PCI_OP_SET_INTERRUPT_MODE, /*rd_handle=*/nullptr, /*wr_handle=*/nullptr, &req,
                &resp);
}

zx_status_t FragmentProxy::PciGetDeviceInfo(pci_device_info_t* out_info) {
  PciRpcRequest req{};
  PciRpcResponse resp{};
  zx_status_t st = PciRpc(pci::PCI_OP_GET_DEVICE_INFO, /*rd_handle=*/nullptr,
                          /*wr_handle=*/nullptr, &req, &resp);
  if (st == ZX_OK) {
    *out_info = resp.info;
  }
  return st;
}

template <typename T>
zx_status_t FragmentProxy::PciReadConfig(uint16_t offset, T* out_value) {
  PciRpcRequest req{};
  PciRpcResponse resp{};

  req.cfg.offset = offset;
  req.cfg.width = static_cast<uint16_t>(sizeof(T));
  zx_status_t st = PciRpc(pci::PCI_OP_CONFIG_READ, /*rd_handle=*/nullptr,
                          /*wr_handle=*/nullptr, &req, &resp);
  if (st == ZX_OK) {
    *out_value = static_cast<T>(resp.cfg.value);
  }
  return st;
}

zx_status_t FragmentProxy::PciReadConfig8(uint16_t offset, uint8_t* out_value) {
  return PciReadConfig(offset, out_value);
}

zx_status_t FragmentProxy::PciReadConfig16(uint16_t offset, uint16_t* out_value) {
  return PciReadConfig(offset, out_value);
}

zx_status_t FragmentProxy::PciReadConfig32(uint16_t offset, uint32_t* out_value) {
  return PciReadConfig(offset, out_value);
}

template <typename T>
zx_status_t FragmentProxy::PciWriteConfig(uint16_t offset, T value) {
  PciRpcRequest req{};
  PciRpcResponse resp{};

  req.cfg.offset = offset;
  req.cfg.width = static_cast<uint16_t>(sizeof(T));
  req.cfg.value = value;
  return PciRpc(pci::PCI_OP_CONFIG_WRITE, /*rd_handle=*/nullptr, /*wr_handle=*/nullptr, &req,
                &resp);
}

zx_status_t FragmentProxy::PciWriteConfig8(uint16_t offset, uint8_t value) {
  return PciWriteConfig(offset, value);
}

zx_status_t FragmentProxy::PciWriteConfig16(uint16_t offset, uint16_t value) {
  return PciWriteConfig(offset, value);
}

zx_status_t FragmentProxy::PciWriteConfig32(uint16_t offset, uint32_t value) {
  return PciWriteConfig(offset, value);
}

zx_status_t FragmentProxy::PciGetFirstCapability(uint8_t cap_id, uint8_t* out_offset) {
  return PciGetNextCapability(cap_id, pci::kPciCapOffsetFirst, out_offset);
}

zx_status_t FragmentProxy::PciGetNextCapability(uint8_t cap_id, uint8_t offset,
                                                uint8_t* out_offset) {
  if (!out_offset) {
    return ZX_ERR_INVALID_ARGS;
  }

  PciRpcRequest req{};
  req.cap.id = cap_id;
  if (offset == pci::kPciCapOffsetFirst) {
    req.cap.is_first = true;
    req.cap.offset = 0;
  } else {
    req.cap.offset = offset;
  }

  PciRpcResponse resp{};
  zx_status_t st = PciRpc(pci::PCI_OP_GET_NEXT_CAPABILITY, /*rd_handle=*/nullptr,
                          /*wr_handle=*/nullptr, &req, &resp);
  if (st == ZX_OK) {
    *out_offset = static_cast<uint8_t>(resp.cap.offset);
  }
  return st;
}

zx_status_t FragmentProxy::PciGetFirstExtendedCapability(uint16_t cap_id, uint16_t* out_offset) {
  return PciGetNextExtendedCapability(cap_id, pci::kPciExtCapOffsetFirst, out_offset);
}

zx_status_t FragmentProxy::PciGetNextExtendedCapability(uint16_t cap_id, uint16_t offset,
                                                        uint16_t* out_offset) {
  if (!out_offset) {
    return ZX_ERR_INVALID_ARGS;
  }

  PciRpcRequest req{};
  req.cap.id = cap_id;
  if (offset == pci::kPciExtCapOffsetFirst) {
    req.cap.is_first = true;
    req.cap.offset = 0;
  } else {
    req.cap.offset = offset;
  }
  req.cap.is_extended = true;

  PciRpcResponse resp{};
  zx_status_t st = PciRpc(pci::PCI_OP_GET_NEXT_CAPABILITY, /*rd_handle=*/nullptr,
                          /*wr_handle=*/nullptr, &req, &resp);
  if (st == ZX_OK) {
    *out_offset = resp.cap.offset;
  }
  return st;
}

zx_status_t FragmentProxy::PciGetBti(uint32_t index, zx::bti* out_bti) {
  PciRpcRequest req{};
  PciRpcResponse resp{};
  req.bti_index = index;
  zx_handle_t handle;
  zx_status_t st =
      PciRpc(pci::PCI_OP_GET_BTI, /*rd_handle=*/&handle, /*wr_handle=*/nullptr, &req, &resp);
  if (st == ZX_OK) {
    out_bti->reset(handle);
  }
  return st;
}

}  // namespace fragment
