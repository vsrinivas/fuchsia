// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/bti.h>
#include <lib/zx/channel.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>
// TODO(fxbug.dev/33713): Stop depending on the types in this file.
#include <fuchsia/hardware/pci/c/banjo.h>
#include <zircon/syscalls/pci.h>
#include <zircon/types.h>

#include "common.h"
#include "device.h"
#include "device_rpc.h"

#define RPC_ENTRY zxlogf(DEBUG, "[%s] %s: entry", cfg_->addr(), __func__)

#define RPC_UNIMPLEMENTED \
  RPC_ENTRY;              \
  return RpcReply(ch, ZX_ERR_NOT_SUPPORTED)

namespace pci {

zx_status_t Device::DdkRxrpc(zx_handle_t channel) {
  if (channel == ZX_HANDLE_INVALID) {
    // A new connection has been made, there's nothing else to do.
    return ZX_OK;
  }

  // Clear the buffers. We only servce new requests after we've finished
  // previous messages, so we won't overwrite data here.
  memset(&request_, 0, sizeof(request_));
  memset(&response_, 0, sizeof(response_));

  uint32_t bytes_in;
  uint32_t handles_in;
  zx_handle_t handle;
  zx::unowned_channel ch(channel);
  zx_status_t st = ch->read(0, &request_, &handle, sizeof(request_), 1, &bytes_in, &handles_in);
  if (st != ZX_OK) {
    return ZX_ERR_INTERNAL;
  }

  if (bytes_in != sizeof(request_)) {
    return ZX_ERR_INTERNAL;
  }

  {
    fbl::AutoLock dev_lock(&dev_lock_);
    if (disabled_) {
      return RpcReply(ch, ZX_ERR_BAD_STATE);
    }
  }

  switch (request_.op) {
    case PCI_OP_CONFIG_READ:
      return RpcConfigRead(ch);
    case PCI_OP_CONFIG_WRITE:
      return RpcConfigWrite(ch);
    case PCI_OP_CONFIGURE_IRQ_MODE:
      return RpcConfigureIrqMode(ch);
    case PCI_OP_CONNECT_SYSMEM:
      return RpcConnectSysmem(ch, handle);
    case PCI_OP_ENABLE_BUS_MASTER:
      return RpcEnableBusMaster(ch);
    case PCI_OP_GET_BAR:
      return RpcGetBar(ch);
    case PCI_OP_GET_BTI:
      return RpcGetBti(ch);
    case PCI_OP_GET_DEVICE_INFO:
      return RpcGetDeviceInfo(ch);
    case PCI_OP_GET_NEXT_CAPABILITY:
      return RpcGetNextCapability(ch);
    case PCI_OP_MAP_INTERRUPT:
      return RpcMapInterrupt(ch);
    case PCI_OP_QUERY_IRQ_MODE:
      return RpcQueryIrqMode(ch);
    case PCI_OP_RESET_DEVICE:
      return RpcResetDevice(ch);
    case PCI_OP_SET_IRQ_MODE:
      return RpcSetIrqMode(ch);
    default:
      return RpcReply(ch, ZX_ERR_INVALID_ARGS);
  };

  return ZX_OK;
}

// Utility method to handle setting up the payload to return to the proxy and common
// error situations.
zx_status_t Device::RpcReply(const zx::unowned_channel& ch, zx_status_t st, zx_handle_t* handles,
                             const uint32_t handle_cnt) {
  response_.op = request_.op;
  response_.txid = request_.txid;
  response_.ret = st;
  return ch->write(0, &response_, sizeof(response_), handles, handle_cnt);
}

zx_status_t Device::RpcConfigRead(const zx::unowned_channel& ch) {
  response_.cfg.width = request_.cfg.width;
  response_.cfg.offset = request_.cfg.offset;

  if (request_.cfg.offset >= PCI_EXT_CONFIG_SIZE) {
    return RpcReply(ch, ZX_ERR_OUT_OF_RANGE);
  }

  switch (request_.cfg.width) {
    case 1:
      response_.cfg.value = cfg_->Read(PciReg8(request_.cfg.offset));
      break;
    case 2:
      response_.cfg.value = cfg_->Read(PciReg16(request_.cfg.offset));
      break;
    case 4:
      response_.cfg.value = cfg_->Read(PciReg32(request_.cfg.offset));
      break;
    default:
      return RpcReply(ch, ZX_ERR_INVALID_ARGS);
  }

  zxlogf(TRACE, "[%s] Read%u[%#x] = %#x", cfg_->addr(), request_.cfg.width * 8, request_.cfg.offset,
         response_.cfg.value);
  return RpcReply(ch, ZX_OK);
}

zx_status_t Device::RpcConfigWrite(const zx::unowned_channel& ch) {
  response_.cfg.width = request_.cfg.width;
  response_.cfg.offset = request_.cfg.offset;
  response_.cfg.value = request_.cfg.value;

  // Don't permit writes inside the config header.
  if (request_.cfg.offset < PCI_CONFIG_HDR_SIZE) {
    return RpcReply(ch, ZX_ERR_ACCESS_DENIED);
  }

  if (request_.cfg.offset >= PCI_EXT_CONFIG_SIZE) {
    return RpcReply(ch, ZX_ERR_OUT_OF_RANGE);
  }

  switch (request_.cfg.width) {
    case 1:
      cfg_->Write(PciReg8(request_.cfg.offset), static_cast<uint8_t>(request_.cfg.value));
      break;
    case 2:
      cfg_->Write(PciReg16(request_.cfg.offset), static_cast<uint16_t>(request_.cfg.value));
      break;
    case 4:
      cfg_->Write(PciReg32(request_.cfg.offset), request_.cfg.value);
      break;
    default:
      return RpcReply(ch, ZX_ERR_INVALID_ARGS);
  }

  zxlogf(TRACE, "[%s] Write%u[%#x] <- %#x", cfg_->addr(), request_.cfg.width * 8,
         request_.cfg.offset, request_.cfg.value);
  return RpcReply(ch, ZX_OK);
}

zx_status_t Device::RpcEnableBusMaster(const zx::unowned_channel& ch) {
  zx_status_t status = EnableBusMaster(request_.enable);
  zxlogf(DEBUG, "[%s] EnableBusMaster { enabled = %u, status = %s }", cfg_->addr(), request_.enable,
         zx_status_get_string(status));
  return RpcReply(ch, status);
}

zx_status_t Device::RpcGetBar(const zx::unowned_channel& ch) {
  fbl::AutoLock dev_lock(&dev_lock_);
  auto bar_id = request_.bar.id;
  if (bar_id >= bar_count_) {
    return RpcReply(ch, ZX_ERR_INVALID_ARGS);
  }

  // If this device supports MSIX then we need to deny access to the BARs it
  // uses.
  // TODO(fxbug.dev/32978): It is technically possible for a device to place the pba/mask
  // tables in the same bar as other data. In that case, we would need to ensure
  // that the bar size reflected the non-table portions, and only allow mapping
  // of that other space.
  auto& msix = caps_.msix;
  if (msix && (msix->table_bar() == bar_id || msix->pba_bar() == bar_id)) {
    return RpcReply(ch, ZX_ERR_ACCESS_DENIED);
  }

  // Both unused BARs and BARs that are the second half of a 64 bit
  // BAR have a size of zero.
  auto& bar = bars_[bar_id];
  if (bar.size == 0) {
    return RpcReply(ch, ZX_ERR_NOT_FOUND);
  }

  zx_handle_t handle = ZX_HANDLE_INVALID;
  uint32_t handle_cnt = 0;
  response_.bar.id = bar_id;
  // MMIO Bars have an associated VMO for the driver to map, whereas
  // IO bars have a Resource corresponding to an IO range for the
  // driver to access. These are mutually exclusive, so only one
  // handle is ever needed.
  zx_status_t status = {};
  if (bar.is_mmio) {
    zx::vmo vmo = {};
    if ((status = bar.allocation->CreateVmObject(&vmo)) == ZX_OK) {
      response_.bar.is_mmio = true;
      handle = vmo.release();
      handle_cnt++;
    }
  } else {  // IO BAR
    zx::resource res = {};
    if (bar.allocation->resource() &&
        (status = bar.allocation->resource().duplicate(ZX_RIGHT_SAME_RIGHTS, &res)) == ZX_OK) {
      response_.bar.is_mmio = false;
      response_.bar.io_addr = static_cast<uint16_t>(bar.address);
      response_.bar.io_size = static_cast<uint16_t>(bar.size);
      handle = res.release();
      handle_cnt++;
    } else {
      status = ZX_ERR_INTERNAL;
      zxlogf(ERROR, "[%s] Failed to create a resource for IO bar %u: %d", cfg_->addr(), bar_id,
             status);
    }
  }

  zxlogf(DEBUG, "[%s] GetBar { bar_id = %u, status = %s }", cfg_->addr(), bar_id,
         zx_status_get_string(status));
  return RpcReply(ch, status, &handle, handle_cnt);
}

zx_status_t Device::RpcConnectSysmem(const zx::unowned_channel& ch, zx_handle_t channel) {
  fbl::AutoLock dev_lock(&dev_lock_);

  zx_status_t st = bdi_->ConnectSysmem(zx::channel(channel));
  zxlogf(DEBUG, "[%s] ConnectSysmem { status = %s }", cfg_->addr(), zx_status_get_string(st));
  return RpcReply(ch, st);
}

zx_status_t Device::RpcGetBti(const zx::unowned_channel& ch) {
  fbl::AutoLock dev_lock(&dev_lock_);

  zx::bti bti;
  zx_handle_t handle = ZX_HANDLE_INVALID;
  uint32_t handle_cnt = 0;
  zx_status_t st = bdi_->GetBti(this, request_.bti_index, &bti);
  if (st == ZX_OK) {
    handle = bti.release();
    handle_cnt++;
  }

  zxlogf(DEBUG, "[%s] GetBti { index = %u, status = %s }", cfg_->addr(), request_.bti_index,
         zx_status_get_string(st));
  return RpcReply(ch, st, &handle, handle_cnt);
}

zx_status_t Device::RpcGetDeviceInfo(const zx::unowned_channel& ch) {
  response_.info.vendor_id = vendor_id();
  response_.info.device_id = device_id();
  response_.info.base_class = class_id();
  response_.info.sub_class = subclass();
  response_.info.program_interface = prog_if();
  response_.info.revision_id = rev_id();
  response_.info.bus_id = bus_id();
  response_.info.dev_id = dev_id();
  response_.info.func_id = func_id();

  return RpcReply(ch, ZX_OK);
}

namespace {
template <class T, class L>
zx_status_t GetNextCapability(PciRpcMsg* req, PciRpcMsg* resp, const L* list) {
  resp->cap.id = req->cap.id;
  resp->cap.is_extended = req->cap.is_extended;
  resp->cap.is_first = req->cap.is_first;
  // Scan for the capability type requested, returning the first capability
  // found after we've seen the capability owning the previous offset.  We
  // can't scan entirely based on offset being >= than a given base because
  // capabilities pointers can point backwards in config space as long as the
  // structures are valid.
  zx_status_t st = ZX_ERR_NOT_FOUND;
  bool found_prev = (req->cap.is_first) ? true : false;
  T scan_offset = static_cast<T>(req->cap.offset);

  for (auto& cap : *list) {
    if (found_prev) {
      if (cap.id() == req->cap.id) {
        resp->cap.offset = cap.base();
        st = ZX_OK;
        break;
      }
    } else {
      if (cap.base() == scan_offset) {
        found_prev = true;
      }
    }
  }
  return st;
}

}  // namespace
zx_status_t Device::RpcGetNextCapability(const zx::unowned_channel& ch) {
  // Capabilities and Extended Capabilities only differ by what list they're in along with the
  // size of their entries. We can offload most of the work into a templated work function.
  zx_status_t st = ZX_ERR_NOT_FOUND;
  if (request_.cap.is_extended) {
    st = GetNextCapability<uint16_t, ExtCapabilityList>(&request_, &response_,
                                                        &capabilities().ext_list);
  } else {
    st = GetNextCapability<uint8_t, CapabilityList>(&request_, &response_, &capabilities().list);
  }
  return RpcReply(ch, st);
}

zx_status_t Device::RpcConfigureIrqMode(const zx::unowned_channel& ch) {
  uint32_t irq_cnt = request_.irq.requested_irqs;
  std::array<pci_irq_mode_t, 2> modes{PCI_IRQ_MODE_MSI_X, PCI_IRQ_MODE_MSI};
  for (auto& mode : modes) {
    if (auto result = QueryIrqMode(mode); result.is_ok() && result.value() >= irq_cnt) {
      zx_status_t st = SetIrqMode(mode, irq_cnt);
      zxlogf(DEBUG, "[%s] ConfigureIrqMode { mode = %u, requested_irqs = %u, status = %s }",
             cfg_->addr(), mode, irq_cnt, zx_status_get_string(st));
      return RpcReply(ch, st);
    }
  }

  zxlogf(DEBUG, "[%s] ConfigureIrqMode { no valid modes found }", cfg_->addr());
  return RpcReply(ch, ZX_ERR_NOT_SUPPORTED);
}

zx_status_t Device::RpcQueryIrqMode(const zx::unowned_channel& ch) {
  response_.irq.max_irqs = 0;
  auto result = QueryIrqMode(request_.irq.mode);
  if (result.is_ok()) {
    response_.irq.max_irqs = result.value();
  }

  zxlogf(DEBUG, "[%s] QueryIrqMode { mode = %u, max_irqs = %u, status = %s }", cfg_->addr(),
         request_.irq.mode, response_.irq.max_irqs, result.status_string());
  return RpcReply(ch, result.status_value());
}

zx_status_t Device::RpcSetIrqMode(const zx::unowned_channel& ch) {
  zx_status_t st = SetIrqMode(request_.irq.mode, request_.irq.requested_irqs);
  zxlogf(DEBUG, "[%s] SetIrqMode { mode = %u, requested_irqs = %u, status = %s }", cfg_->addr(),
         request_.irq.mode, request_.irq.requested_irqs, zx_status_get_string(st));
  return RpcReply(ch, st);
}

zx_status_t Device::RpcMapInterrupt(const zx::unowned_channel& ch) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  size_t handle_cnt = 0;
  zx::status<zx::interrupt> result = MapInterrupt(request_.irq.which_irq);
  if (result.is_ok()) {
    handle_cnt++;
    handle = result.value().release();
  }

  zxlogf(DEBUG, "[%s] MapInterrupt { irq = %u, status = %s }", cfg_->addr(), request_.irq.which_irq,
         zx_status_get_string(result.status_value()));
  return RpcReply(ch, result.status_value(), &handle, handle_cnt);
}

zx_status_t Device::RpcResetDevice(const zx::unowned_channel& ch) { RPC_UNIMPLEMENTED; }

}  // namespace pci
