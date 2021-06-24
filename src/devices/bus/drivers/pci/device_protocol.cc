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

#include "src/devices/bus/drivers/pci/common.h"
#include "src/devices/bus/drivers/pci/device.h"
#include "src/devices/bus/drivers/pci/proxy_rpc.h"

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
    case PCI_OP_ACK_INTERRUPT:
      return RpcAckInterrupt(ch);
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
  fbl::AutoLock dev_lock(&dev_lock_);

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
  // MMIO Bars have an associated VMO for the driver to map, whereas IO bars
  // have a Resource corresponding to an IO range for the driver to access.
  // These are mutually exclusive, so only one handle is ever needed.
  zx_status_t status = {};
  if (bar.is_mmio) {
    zx::vmo vmo = {};
    if ((status = bar.allocation->CreateVmObject(&vmo)) == ZX_OK) {
      response_.bar.is_mmio = true;
      response_.bar.size = bar.size;
      handle = vmo.release();
      handle_cnt++;
    }
  } else {  // Bar using IOports
    zx::resource res = {};
    if (bar.allocation->resource() &&
        (status = bar.allocation->resource().duplicate(ZX_RIGHT_SAME_RIGHTS, &res)) == ZX_OK) {
      response_.bar.is_mmio = false;
      response_.bar.address = static_cast<uint16_t>(bar.address);
      response_.bar.size = static_cast<uint16_t>(bar.size);
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
  std::array<pci_irq_mode_t, 3> modes{PCI_IRQ_MODE_MSI_X, PCI_IRQ_MODE_MSI, PCI_IRQ_MODE_LEGACY};
  for (auto& mode : modes) {
    if (auto result = QueryIrqMode(mode); result.is_ok() && result.value() >= irq_cnt) {
      zx_status_t st = SetIrqMode(mode, irq_cnt);
      zxlogf(DEBUG, "[%s] ConfigureIrqMode { mode = %u, requested_irqs = %u, status = %s }",
             cfg_->addr(), mode, irq_cnt, zx_status_get_string(st));
      if (st == ZX_OK) {
        response_.irq.mode = mode;
      }
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

zx_status_t Device::RpcAckInterrupt(const zx::unowned_channel& ch) {
  fbl::AutoLock dev_lock(&dev_lock_);

  zx_status_t status = AckLegacyIrq();
  return RpcReply(ch, status, nullptr, 0);
}

zx_status_t Device::RpcResetDevice(const zx::unowned_channel& ch) { RPC_UNIMPLEMENTED; }

zx_status_t Device::DdkGetProtocol(uint32_t proto_id, void* out) {
  switch (proto_id) {
    case ZX_PROTOCOL_PCI: {
      auto proto = static_cast<pci_protocol_t*>(out);
      proto->ctx = this;
      proto->ops = &pci_protocol_ops_;
      return ZX_OK;
    }
  }

  return ZX_ERR_NOT_SUPPORTED;
}

template <typename V, typename R>
zx_status_t Device::ConfigRead(uint16_t offset, V* value) {
  if (offset >= PCI_EXT_CONFIG_SIZE) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  *value = cfg_->Read(R(offset));
  return ZX_OK;
}

zx_status_t Device::PciConfigRead8(uint16_t offset, uint8_t* out_value) {
  zx_status_t status = ConfigRead<uint8_t, PciReg8>(offset, out_value);
  zxlogf(TRACE, "%s %s(offset = %#x) = %s", cfg_->addr(), __func__, offset,
         zx_status_get_string(status));
  return status;
}

zx_status_t Device::PciConfigRead16(uint16_t offset, uint16_t* out_value) {
  zx_status_t status = ConfigRead<uint16_t, PciReg16>(offset, out_value);
  zxlogf(TRACE, "%s %s(offset = %#x) = %s", cfg_->addr(), __func__, offset,
         _zx_status_get_string(status));
  return status;
}

zx_status_t Device::PciConfigRead32(uint16_t offset, uint32_t* out_value) {
  zx_status_t status = ConfigRead<uint32_t, PciReg32>(offset, out_value);
  zxlogf(TRACE, "%s %s(offset = %#x) = %s", cfg_->addr(), __func__, offset,
         _zx_status_get_string(status));
  return status;
}

template <typename V, typename R>
zx_status_t Device::ConfigWrite(uint16_t offset, V value) {
  // Don't permit writes inside the config header.
  if (offset < PCI_CONFIG_HDR_SIZE) {
    return ZX_ERR_ACCESS_DENIED;
  }

  if (offset >= PCI_EXT_CONFIG_SIZE) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  cfg_->Write(R(offset), value);
  return ZX_OK;
}

zx_status_t Device::PciConfigWrite8(uint16_t offset, uint8_t value) {
  return ConfigWrite<uint8_t, PciReg8>(offset, value);
}

zx_status_t Device::PciConfigWrite16(uint16_t offset, uint16_t value) {
  return ConfigWrite<uint16_t, PciReg16>(offset, value);
}

zx_status_t Device::PciConfigWrite32(uint16_t offset, uint32_t value) {
  return ConfigWrite<uint32_t, PciReg32>(offset, value);
}

zx_status_t Device::PciEnableBusMaster(bool enable) {
  fbl::AutoLock dev_lock(&dev_lock_);
  return EnableBusMaster(enable);
}

zx_status_t Device::PciGetBar(uint32_t bar_id, pci_bar_t* out_bar) {
  fbl::AutoLock dev_lock(&dev_lock_);
  if (bar_id >= bar_count_) {
    return ZX_ERR_INVALID_ARGS;
  }

  // If this device supports MSIX then we need to deny access to the BARs it
  // uses.
  // TODO(fxbug.dev/32978): It is technically possible for a device to place the pba/mask
  // tables in the same bar as other data. In that case, we would need to ensure
  // that the bar size reflected the non-table portions, and only allow mapping
  // of that other space.
  auto& msix = caps_.msix;
  if (msix && (msix->table_bar() == bar_id || msix->pba_bar() == bar_id)) {
    return ZX_ERR_ACCESS_DENIED;
  }

  // Both unused BARs and BARs that are the second half of a 64 bit
  // BAR have a size of zero.
  auto& bar = bars_[bar_id];
  if (bar.size == 0) {
    return ZX_ERR_NOT_FOUND;
  }

  out_bar->id = bar_id;
  out_bar->address = bar.address;
  out_bar->size = bar.size;
  out_bar->type = (bar.is_mmio) ? ZX_PCI_BAR_TYPE_MMIO : ZX_PCI_BAR_TYPE_PIO;

  // MMIO Bars have an associated VMO for the driver to map, whereas IO bars
  // have a Resource corresponding to an IO range for the driver to access.
  // These are mutually exclusive, so only one handle is ever needed.
  zx_status_t status = ZX_ERR_INTERNAL;
  if (bar.is_mmio) {
    zx::vmo vmo = {};
    if ((status = bar.allocation->CreateVmObject(&vmo)) == ZX_OK) {
      out_bar->handle = vmo.release();
    }
  } else {  // Bar using IOports
    zx::resource res = {};
    if (bar.allocation->resource() &&
        (status = bar.allocation->resource().duplicate(ZX_RIGHT_SAME_RIGHTS, &res)) == ZX_OK) {
      out_bar->handle = res.release();
    }
  }

  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to allocate %s [%#lx, %#lx) for BAR %u: %s",
           (bar.is_mmio) ? "MMIO" : "IO", bar.address, bar.address + bar.size, bar.bar_id,
           zx_status_get_string(status));
  }

  return status;
}

zx_status_t Device::PciGetBti(uint32_t index, zx::bti* out_bti) {
  fbl::AutoLock dev_lock(&dev_lock_);
  return bdi_->GetBti(this, index, out_bti);
}

zx_status_t Device::PciGetDeviceInfo(pcie_device_info_t* out_info) {
  out_info->vendor_id = vendor_id();
  out_info->device_id = device_id();
  out_info->base_class = class_id();
  out_info->sub_class = subclass();
  out_info->program_interface = prog_if();
  out_info->revision_id = rev_id();
  out_info->bus_id = bus_id();
  out_info->dev_id = dev_id();
  out_info->func_id = func_id();
  return ZX_OK;
}

namespace {

// Capabilities and Extended Capabilities only differ by what list they're in along with the
// size of their entries. We can offload most of the work into a templated work function.
template <class T, class L>
zx_status_t GetFirstOrNextCapability(const L& list, T cap_id, bool is_first,
                                     std::optional<T> scan_offset, T* out_offset) {
  zxlogf(DEBUG, "Find cap id = %#x, first = %u", cap_id, is_first);
  // Scan for the capability type requested, returning the first capability
  // found after we've seen the capability owning the previous offset.  We
  // can't scan entirely based on offset being >= than a given base because
  // capabilities pointers can point backwards in config space as long as the
  // structures are valid.
  bool found_prev = is_first;

  for (auto& cap : list) {
    if (found_prev) {
      if (cap.id() == cap_id) {
        *out_offset = cap.base();
        return ZX_OK;
      }
    } else {
      if (cap.base() == scan_offset) {
        found_prev = true;
      }
    }
  }
  return ZX_ERR_NOT_FOUND;
}

}  // namespace

zx_status_t Device::PciGetFirstCapability(uint8_t cap_id, uint8_t* out_offset) {
  return GetFirstOrNextCapability<uint8_t, CapabilityList>(
      capabilities().list, cap_id, /*is_first=*/true, std::nullopt, out_offset);
}

zx_status_t Device::PciGetNextCapability(uint8_t cap_id, uint8_t offset, uint8_t* out_offset) {
  return GetFirstOrNextCapability<uint8_t, CapabilityList>(capabilities().list, cap_id,
                                                           /*is_first=*/false, offset, out_offset);
}

zx_status_t Device::PciGetFirstExtendedCapability(uint16_t cap_id, uint16_t* out_offset) {
  return GetFirstOrNextCapability<uint16_t, ExtCapabilityList>(capabilities().ext_list, cap_id,
                                                               true, std::nullopt, out_offset);
}

zx_status_t Device::PciGetNextExtendedCapability(uint16_t cap_id, uint16_t offset,
                                                 uint16_t* out_offset) {
  return GetFirstOrNextCapability<uint16_t, ExtCapabilityList>(capabilities().ext_list, cap_id,
                                                               false, offset, out_offset);
}

zx_status_t Device::PciConfigureIrqMode(uint32_t requested_irq_count, pci_irq_mode_t* out_mode) {
  std::array<pci_irq_mode_t, 3> modes{PCI_IRQ_MODE_MSI_X, PCI_IRQ_MODE_MSI, PCI_IRQ_MODE_LEGACY};
  for (auto& mode : modes) {
    if (auto result = QueryIrqMode(mode); result.is_ok() && result.value() >= requested_irq_count) {
      zx_status_t st = SetIrqMode(mode, requested_irq_count);
      if (st == ZX_OK && out_mode) {
        *out_mode = mode;
      }
      return st;
    }
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::PciQueryIrqMode(pci_irq_mode_t mode, uint32_t* out_max_irqs) {
  auto result = QueryIrqMode(mode);
  if (result.is_ok()) {
    *out_max_irqs = result.value();
  }

  return result.status_value();
}

zx_status_t Device::PciSetIrqMode(pci_irq_mode_t mode, uint32_t requested_irq_count) {
  return SetIrqMode(mode, requested_irq_count);
}

zx_status_t Device::PciMapInterrupt(uint32_t which_irq, zx::interrupt* out_handle) {
  zx::status<zx::interrupt> result = MapInterrupt(which_irq);
  if (result.is_ok()) {
    *out_handle = std::move(result.value());
  }

  return result.status_value();
}

zx_status_t Device::PciAckInterrupt() {
  fbl::AutoLock dev_lock(&dev_lock_);
  return AckLegacyIrq();
}

zx_status_t Device::PciResetDevice() { return ZX_ERR_NOT_SUPPORTED; }
}  // namespace pci
