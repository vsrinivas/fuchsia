// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/zx/status.h>

#include "src/devices/bus/drivers/pci/proxy_rpc.h"
#include "src/devices/internal/drivers/fragment/fragment.h"
#include "src/devices/internal/drivers/fragment/proxy-protocol.h"

namespace fragment {

zx_status_t RpcEnableBusMaster(const ddk::PciProtocolClient& pci, const PciRpcRequest* req,
                               PciRpcResponse* resp) {
  return pci.EnableBusMaster(req->enable);
}

zx_status_t RpcResetDevice(const ddk::PciProtocolClient& pci, const PciRpcRequest* req,
                           PciRpcResponse* resp) {
  return pci.ResetDevice();
}

// Reads from a config space address for a given device handle. Most of the
// heavy lifting is offloaded to the zx_pci_config_read syscall itself, and the
// rpc client that formats the arguments.
zx_status_t RpcConfigRead(const ddk::PciProtocolClient& pci, const PciRpcRequest* req,
                          PciRpcResponse* resp) {
  resp->cfg = req->cfg;
  switch (req->cfg.width) {
    case 1:
      return pci.ConfigRead8(req->cfg.offset, reinterpret_cast<uint8_t*>(&resp->cfg.value));
    case 2:
      return pci.ConfigRead16(req->cfg.offset, reinterpret_cast<uint16_t*>(&resp->cfg.value));
    case 4:
      return pci.ConfigRead32(req->cfg.offset, &resp->cfg.value);
  }

  return ZX_ERR_INVALID_ARGS;
}

zx_status_t RpcConfigWrite(const ddk::PciProtocolClient& pci, const PciRpcRequest* req,
                           PciRpcResponse* resp) {
  switch (req->cfg.width) {
    case 1:
      return pci.ConfigWrite8(req->cfg.offset, req->cfg.value);
    case 2:
      return pci.ConfigWrite16(req->cfg.offset, req->cfg.value);
    case 4:
      return pci.ConfigWrite32(req->cfg.offset, req->cfg.value);
  }
  return ZX_ERR_INVALID_ARGS;
}

// Retrieves either address information for PIO or a VMO corresponding to a
// device's bar to pass back to the devhost making the call.
zx_status_t RpcGetBar(const ddk::PciProtocolClient& pci, const PciRpcRequest* req,
                      PciRpcResponse* resp, zx::handle* handle) {
  pci_bar_t bar{};
  zx_status_t st = pci.GetBar(req->bar.id, &bar);
  if (st == ZX_OK) {
    resp->bar = {
        .id = bar.id,
        .is_mmio = (bar.type == ZX_PCI_BAR_TYPE_MMIO),
        .size = bar.size,
        .address = bar.address,
    };

    handle->reset(bar.handle);
  }

  return st;
}

zx_status_t RpcGetInterruptModes(const ddk::PciProtocolClient& pci, const PciRpcRequest* req,
                                 PciRpcResponse* resp) {
  pci.GetInterruptModes(&resp->irq.modes);
  return ZX_OK;
}

zx_status_t RpcSetInterruptMode(const ddk::PciProtocolClient& pci, const PciRpcRequest* req,
                                PciRpcResponse* resp) {
  return pci.SetInterruptMode(req->irq.mode, req->irq.requested_irqs);
}

zx_status_t RpcGetNextCapability(const ddk::PciProtocolClient& pci, const PciRpcRequest* req,
                                 PciRpcResponse* resp) {
  auto* offset_cast = reinterpret_cast<uint8_t*>(&resp->cap.offset);
  return (req->cap.is_first) ? pci.GetFirstCapability(req->cap.id, offset_cast)
                             : pci.GetNextCapability(req->cap.id, req->cap.offset, offset_cast);
}

zx_status_t RpcMapInterrupt(const ddk::PciProtocolClient& pci, const PciRpcRequest* req,
                            PciRpcResponse* resp, zx::handle* handle) {
  zx::interrupt interrupt;
  zx_status_t st = pci.MapInterrupt(req->irq.which_irq, &interrupt);
  if (st == ZX_OK) {
    *handle = std::move(interrupt);
  }
  return st;
}

zx_status_t RpcAckInterrupt(const ddk::PciProtocolClient& pci, const PciRpcRequest* req,
                            PciRpcResponse* resp) {
  return pci.AckInterrupt();
}

zx_status_t RpcGetDeviceInfo(const ddk::PciProtocolClient& pci, const PciRpcRequest* req,
                             PciRpcResponse* resp) {
  return pci.GetDeviceInfo(&resp->info);
}

zx_status_t RpcGetBti(const ddk::PciProtocolClient& pci, const PciRpcRequest* req,
                      PciRpcResponse* resp, zx::handle* handle) {
  zx::bti bti;
  zx_status_t st = pci.GetBti(req->bti_index, &bti);
  if (st == ZX_OK) {
    *handle = std::move(bti);
  }

  return st;
}

zx_status_t Fragment::RpcPci(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                             uint32_t* out_resp_size, zx::handle* req_handles,
                             uint32_t req_handle_count, zx::handle* resp_handles,
                             uint32_t* resp_handle_count) {
  auto* request = reinterpret_cast<const PciRpcRequest*>(req_buf);
  auto* response = reinterpret_cast<PciRpcResponse*>(resp_buf);
  zx_status_t status = ZX_ERR_INVALID_ARGS;
  resp_handles->reset();

  switch (request->op) {
    case pci::PCI_OP_CONFIG_READ:
      status = RpcConfigRead(pci_client_.proto_client(), request, response);
      break;
    case pci::PCI_OP_CONFIG_WRITE:
      status = RpcConfigWrite(pci_client_.proto_client(), request, response);
      break;
    case pci::PCI_OP_ENABLE_BUS_MASTER:
      status = RpcEnableBusMaster(pci_client_.proto_client(), request, response);
      break;
    case pci::PCI_OP_GET_BAR:
      status = RpcGetBar(pci_client_.proto_client(), request, response, resp_handles);
      break;
    case pci::PCI_OP_GET_BTI:
      status = RpcGetBti(pci_client_.proto_client(), request, response, resp_handles);
      break;
    case pci::PCI_OP_GET_DEVICE_INFO:
      status = RpcGetDeviceInfo(pci_client_.proto_client(), request, response);
      break;
    case pci::PCI_OP_GET_NEXT_CAPABILITY:
      status = RpcGetNextCapability(pci_client_.proto_client(), request, response);
      break;
    case pci::PCI_OP_MAP_INTERRUPT:
      status = RpcMapInterrupt(pci_client_.proto_client(), request, response, resp_handles);
      break;
    case pci::PCI_OP_GET_INTERRUPT_MODES:
      status = RpcGetInterruptModes(pci_client_.proto_client(), request, response);
      break;
    case pci::PCI_OP_RESET_DEVICE:
      status = RpcResetDevice(pci_client_.proto_client(), request, response);
      break;
    case pci::PCI_OP_SET_INTERRUPT_MODE:
      status = RpcSetInterruptMode(pci_client_.proto_client(), request, response);
      break;
    case pci::PCI_OP_ACK_INTERRUPT:
      status = RpcAckInterrupt(pci_client_.proto_client(), request, response);
      break;
    default:
      status = ZX_ERR_INVALID_ARGS;
  };

  if (status == ZX_OK && resp_handles->get() != ZX_HANDLE_INVALID) {
    *resp_handle_count = 1;
  }

  *out_resp_size = sizeof(PciRpcResponse);
  return status;
}

}  // namespace fragment
