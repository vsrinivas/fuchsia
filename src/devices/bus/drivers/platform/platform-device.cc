// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform-device.h"

#include <assert.h>
#include <lib/zircon-internal/align.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/syscalls/resource.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <fbl/function.h>

#include "platform-bus.h"

namespace platform_bus {

zx_status_t PlatformDevice::Create(const pbus_dev_t* pdev, zx_device_t* parent, PlatformBus* bus,
                                   Type type, std::unique_ptr<platform_bus::PlatformDevice>* out) {
  fbl::AllocChecker ac;
  std::unique_ptr<platform_bus::PlatformDevice> dev(
      new (&ac) platform_bus::PlatformDevice(parent, bus, type, pdev));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Init(pdev);
  if (status != ZX_OK) {
    return status;
  }
  out->swap(dev);
  return ZX_OK;
}

PlatformDevice::PlatformDevice(zx_device_t* parent, PlatformBus* bus, Type type,
                               const pbus_dev_t* pdev)
    : PlatformDeviceType(parent),
      bus_(bus),
      type_(type),
      vid_(pdev->vid),
      pid_(pdev->pid),
      did_(pdev->did) {
  strlcpy(name_, pdev->name, sizeof(name_));
}

zx_status_t PlatformDevice::Init(const pbus_dev_t* pdev) {
  auto status = resources_.Init(pdev);
  if (status != ZX_OK) {
    return status;
  }

  if (type_ == Protocol) {
    // Protocol devices implement a subset of the platform bus protocol.
    pbus_protocol_t pbus;
    status = device_get_protocol(parent(), ZX_PROTOCOL_PBUS, &pbus);
    if (status != ZX_OK) {
      return status;
    }

    pbus_ctx_ = pbus.ctx;
    // Make a copy of the platform bus protocol so we can replace some methods.
    pbus_ops_ = *pbus.ops;

    // Do not allow calling device_add, protocol_device_add and composite_device_add.
    // Only the board driver should be calling those.
    pbus_ops_.device_add = [](void* ctx, const pbus_dev_t* dev) { return ZX_ERR_NOT_SUPPORTED; };
    pbus_ops_.protocol_device_add = [](void* ctx, uint32_t proto_id, const pbus_dev_t* dev) {
      return ZX_ERR_NOT_SUPPORTED;
    };
    pbus_ops_.composite_device_add =
        [](void* ctx, const pbus_dev_t* dev, const device_fragment_t* fragments_list,
           size_t fragments_count,
           uint32_t t_coresident_device_index) { return ZX_ERR_NOT_SUPPORTED; };
  }

  return ZX_OK;
}

zx_status_t PlatformDevice::PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) {
  if (index >= resources_.mmio_count()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const pbus_mmio_t& mmio = resources_.mmio(index);
  const zx_paddr_t vmo_base = ZX_ROUNDDOWN(mmio.base, ZX_PAGE_SIZE);
  const size_t vmo_size = ZX_ROUNDUP(mmio.base + mmio.length - vmo_base, ZX_PAGE_SIZE);
  zx::vmo vmo;

  zx_status_t status = zx::vmo::create_physical(*bus_->GetResource(), vmo_base, vmo_size, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: creating vmo failed %d\n", __FUNCTION__, status);
    return status;
  }

  char name[32];
  snprintf(name, sizeof(name), "mmio %u", index);
  status = vmo.set_property(ZX_PROP_NAME, name, sizeof(name));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: setting vmo name failed %d\n", __FUNCTION__, status);
    return status;
  }

  out_mmio->offset = mmio.base - vmo_base;
  out_mmio->vmo = vmo.release();
  out_mmio->size = mmio.length;
  return ZX_OK;
}

zx_status_t PlatformDevice::PDevGetInterrupt(uint32_t index, uint32_t flags,
                                             zx::interrupt* out_irq) {
  if (index >= resources_.irq_count()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (out_irq == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  const pbus_irq_t& irq = resources_.irq(index);
  if (flags == 0) {
    flags = irq.mode;
  }
  zx_status_t status = zx::interrupt::create(*bus_->GetResource(), irq.irq, flags, out_irq);
  if (status != ZX_OK) {
    zxlogf(ERROR, "platform_dev_map_interrupt: zx_interrupt_create failed %d\n", status);
    return status;
  }
  return status;
}

zx_status_t PlatformDevice::PDevGetBti(uint32_t index, zx::bti* out_bti) {
  if (index >= resources_.bti_count()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (out_bti == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  const pbus_bti_t& bti = resources_.bti(index);

  return bus_->IommuGetBti(bti.iommu_index, bti.bti_id, out_bti);
}

zx_status_t PlatformDevice::PDevGetSmc(uint32_t index, zx::resource* out_resource) {
  if (index >= resources_.smc_count()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (out_resource == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  const pbus_smc_t& smc = resources_.smc(index);

  uint32_t options = ZX_RSRC_KIND_SMC;
  if (smc.exclusive)
    options |= ZX_RSRC_FLAG_EXCLUSIVE;
  char rsrc_name[ZX_MAX_NAME_LEN];
  snprintf(rsrc_name, ZX_MAX_NAME_LEN - 1, "%s.pbus[%u]", name_, index);
  return zx::resource::create(*bus_->GetResource(), options, smc.service_call_num_base, smc.count,
                              rsrc_name, sizeof(rsrc_name), out_resource);
}

zx_status_t PlatformDevice::PDevGetDeviceInfo(pdev_device_info_t* out_info) {
  pdev_device_info_t info = {
      .vid = vid_,
      .pid = pid_,
      .did = did_,
      .mmio_count = static_cast<uint32_t>(resources_.mmio_count()),
      .irq_count = static_cast<uint32_t>(resources_.irq_count()),
      .bti_count = static_cast<uint32_t>(resources_.bti_count()),
      .smc_count = static_cast<uint32_t>(resources_.smc_count()),
      .metadata_count = static_cast<uint32_t>(resources_.metadata_count()),
      .reserved = {},
      .name = {},
  };
  static_assert(sizeof(info.name) == sizeof(name_), "");
  memcpy(info.name, name_, sizeof(out_info->name));
  memcpy(out_info, &info, sizeof(info));

  return ZX_OK;
}

zx_status_t PlatformDevice::PDevGetBoardInfo(pdev_board_info_t* out_info) {
  return bus_->PBusGetBoardInfo(out_info);
}

zx_status_t PlatformDevice::PDevDeviceAdd(uint32_t index, const device_add_args_t* args,
                                          zx_device_t** device) {
  return ZX_ERR_NOT_SUPPORTED;
}

// Create a resource and pass it back to the proxy along with necessary metadata
// to create/map the VMO in the driver process.
zx_status_t PlatformDevice::RpcGetMmio(uint32_t index, zx_paddr_t* out_paddr, size_t* out_length,
                                       zx_handle_t* out_handle, uint32_t* out_handle_count) {
  if (index >= resources_.mmio_count()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  const auto& root_rsrc = bus_->GetResource();
  if (!root_rsrc->is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }

  const pbus_mmio_t& mmio = resources_.mmio(index);
  zx::resource resource;
  char rsrc_name[ZX_MAX_NAME_LEN];
  snprintf(rsrc_name, ZX_MAX_NAME_LEN - 1, "%s.pbus[%u]", name_, index);
  zx_status_t status = zx::resource::create(*root_rsrc, ZX_RSRC_KIND_MMIO, mmio.base, mmio.length,
                                            rsrc_name, sizeof(rsrc_name), &resource);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_rpc_get_mmio: zx_resource_create failed: %d\n", name_, status);
    return status;
  }

  *out_paddr = mmio.base;
  *out_length = mmio.length;
  *out_handle_count = 1;
  *out_handle = resource.release();
  return ZX_OK;
}

// Create a resource and pass it back to the proxy along with necessary metadata
// to create the IRQ in the driver process.
zx_status_t PlatformDevice::RpcGetInterrupt(uint32_t index, uint32_t* out_irq, uint32_t* out_mode,
                                            zx_handle_t* out_handle, uint32_t* out_handle_count) {
  if (index >= resources_.irq_count()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const auto& root_rsrc = bus_->GetResource();
  if (!root_rsrc->is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }

  zx::resource resource;
  const pbus_irq_t& irq = resources_.irq(index);
  uint32_t options = ZX_RSRC_KIND_IRQ | ZX_RSRC_FLAG_EXCLUSIVE;
  char rsrc_name[ZX_MAX_NAME_LEN];
  snprintf(rsrc_name, ZX_MAX_NAME_LEN - 1, "%s.pbus[%u]", name_, index);
  zx_status_t status = zx::resource::create(*root_rsrc, options, irq.irq, 1, rsrc_name,
                                            sizeof(rsrc_name), &resource);
  if (status != ZX_OK) {
    return status;
  }

  *out_irq = irq.irq;
  *out_mode = irq.mode;
  *out_handle_count = 1;
  *out_handle = resource.release();
  return ZX_OK;
}

zx_status_t PlatformDevice::RpcGetBti(uint32_t index, zx_handle_t* out_handle,
                                      uint32_t* out_handle_count) {
  if (index >= resources_.bti_count()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const pbus_bti_t& bti = resources_.bti(index);

  zx::bti out_bti;
  zx_status_t status = bus_->IommuGetBti(bti.iommu_index, bti.bti_id, &out_bti);
  *out_handle = out_bti.release();

  if (status == ZX_OK) {
    *out_handle_count = 1;
  }

  return status;
}

zx_status_t PlatformDevice::RpcGetSmc(uint32_t index, zx_handle_t* out_handle,
                                      uint32_t* out_handle_count) {
  if (index >= resources_.smc_count()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const auto& root_rsrc = bus_->GetResource();
  if (!root_rsrc->is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }

  zx::resource resource;
  const pbus_smc_t& smc = resources_.smc(index);
  uint32_t options = ZX_RSRC_KIND_SMC;
  if (smc.exclusive)
    options |= ZX_RSRC_FLAG_EXCLUSIVE;
  char rsrc_name[ZX_MAX_NAME_LEN];
  snprintf(rsrc_name, ZX_MAX_NAME_LEN - 1, "%s.pbus[%u]", name_, index);
  zx_status_t status = zx::resource::create(*root_rsrc, options, smc.service_call_num_base,
                                            smc.count, rsrc_name, sizeof(rsrc_name), &resource);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_rpc_get_smc: zx_resource_create failed: %d\n", name_, status);
    return status;
  }

  *out_handle_count = 1;
  *out_handle = resource.release();
  return ZX_OK;
}

zx_status_t PlatformDevice::RpcGetDeviceInfo(pdev_device_info_t* out_info) {
  pdev_device_info_t info = {
      .vid = vid_,
      .pid = pid_,
      .did = did_,
      .mmio_count = static_cast<uint32_t>(resources_.mmio_count()),
      .irq_count = static_cast<uint32_t>(resources_.irq_count()),
      .bti_count = static_cast<uint32_t>(resources_.bti_count()),
      .smc_count = static_cast<uint32_t>(resources_.smc_count()),
      .metadata_count =
          static_cast<uint32_t>(resources_.metadata_count() + resources_.boot_metadata_count()),
      .reserved = {},
      .name = {},
  };
  static_assert(sizeof(info.name) == sizeof(name_), "");
  memcpy(info.name, name_, sizeof(out_info->name));
  memcpy(out_info, &info, sizeof(info));

  return ZX_OK;
}

zx_status_t PlatformDevice::RpcGetMetadata(uint32_t index, uint32_t* out_type, uint8_t* buf,
                                           uint32_t buf_size, uint32_t* actual) {
  if (index >= resources_.metadata_count() + resources_.boot_metadata_count()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (index < resources_.metadata_count()) {
    auto& metadata = resources_.metadata(index);
    if (metadata.data_size > buf_size) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(buf, metadata.data_buffer, metadata.data_size);
    *out_type = metadata.type;
    *actual = static_cast<uint32_t>(metadata.data_size);
    return ZX_OK;
  }

  // boot_metadata indices follow metadata indices.
  index -= static_cast<uint32_t>(resources_.metadata_count());

  auto& metadata = resources_.boot_metadata(index);
  zx::vmo vmo;
  uint32_t length;
  zx_status_t status = bus_->GetBootItem(metadata.zbi_type, metadata.zbi_extra, &vmo, &length);
  if (status != ZX_OK) {
    return status;
  } else if (length > buf_size) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  status = vmo.read(buf, 0, length);
  if (status != ZX_OK) {
    return status;
  }
  *out_type = metadata.zbi_type;
  *actual = length;
  return ZX_OK;
}

zx_status_t PlatformDevice::DdkGetProtocol(uint32_t proto_id, void* out) {
  if (proto_id == ZX_PROTOCOL_PDEV) {
    auto proto = static_cast<pdev_protocol_t*>(out);
    proto->ops = &pdev_protocol_ops_;
    proto->ctx = this;
    return ZX_OK;
  } else if (proto_id == ZX_PROTOCOL_PBUS) {
    // Only protocol devices should have access to the PBUS protocol.
    if (type_ != Protocol) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    // Protocol implementation drivers get a restricted subset of the platform bus protocol.
    auto proto = static_cast<pbus_protocol_t*>(out);
    proto->ops = &pbus_ops_;
    proto->ctx = pbus_ctx_;
    return ZX_OK;
  } else {
    return bus_->DdkGetProtocol(proto_id, out);
  }
}

zx_status_t PlatformDevice::DdkRxrpc(zx_handle_t channel) {
  if (channel == ZX_HANDLE_INVALID) {
    // proxy device has connected
    return ZX_OK;
  }

  uint8_t req_buf[PROXY_MAX_TRANSFER_SIZE];
  uint8_t resp_buf[PROXY_MAX_TRANSFER_SIZE];
  auto* req_header = reinterpret_cast<platform_proxy_req_t*>(&req_buf);
  auto* resp_header = reinterpret_cast<platform_proxy_rsp_t*>(&resp_buf);
  uint32_t actual;
  zx_handle_t req_handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  zx_handle_t resp_handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t req_handle_count;
  uint32_t resp_handle_count = 0;

  auto status = zx_channel_read(channel, 0, &req_buf, req_handles, sizeof(req_buf),
                                fbl::count_of(req_handles), &actual, &req_handle_count);
  if (status != ZX_OK) {
    zxlogf(ERROR, "platform_dev_rxrpc: zx_channel_read failed %d\n", status);
    return status;
  }

  resp_header->txid = req_header->txid;
  uint32_t resp_len;

  auto req = reinterpret_cast<rpc_pdev_req_t*>(&req_buf);
  if (actual < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu (PDEV)\n", __func__, actual, sizeof(*req));
    return ZX_ERR_INTERNAL;
  }
  auto resp = reinterpret_cast<rpc_pdev_rsp_t*>(&resp_buf);
  resp_len = sizeof(*resp);

  switch (req_header->op) {
    case PDEV_GET_MMIO:
      status =
          RpcGetMmio(req->index, &resp->paddr, &resp->length, resp_handles, &resp_handle_count);
      break;
    case PDEV_GET_INTERRUPT:
      status =
          RpcGetInterrupt(req->index, &resp->irq, &resp->mode, resp_handles, &resp_handle_count);
      break;
    case PDEV_GET_BTI:
      status = RpcGetBti(req->index, resp_handles, &resp_handle_count);
      break;
    case PDEV_GET_SMC:
      status = RpcGetSmc(req->index, resp_handles, &resp_handle_count);
      break;
    case PDEV_GET_DEVICE_INFO:
      status = RpcGetDeviceInfo(&resp->device_info);
      break;
    case PDEV_GET_BOARD_INFO:
      status = bus_->PBusGetBoardInfo(&resp->board_info);
      break;
    case PDEV_GET_METADATA: {
      auto resp = reinterpret_cast<rpc_pdev_metadata_rsp_t*>(resp_buf);
      static_assert(sizeof(*resp) == sizeof(resp_buf), "");
      auto buf_size = static_cast<uint32_t>(sizeof(resp_buf) - sizeof(*resp_header));
      status = RpcGetMetadata(req->index, &resp->pdev.metadata_type, resp->metadata, buf_size,
                              &resp->pdev.metadata_length);
      resp_len += resp->pdev.metadata_length;
      break;
    }
    default:
      zxlogf(ERROR, "%s: unknown pdev op %u\n", __func__, req_header->op);
      return ZX_ERR_INTERNAL;
  }

  // set op to match request so zx_channel_write will return our response
  resp_header->status = status;
  status = zx_channel_write(channel, 0, resp_header, resp_len,
                            (resp_handle_count ? resp_handles : nullptr), resp_handle_count);
  if (status != ZX_OK) {
    zxlogf(ERROR, "platform_dev_rxrpc: zx_channel_write failed %d\n", status);
  }
  return status;
}

void PlatformDevice::DdkRelease() { delete this; }

zx_status_t PlatformDevice::Start() {
  char name[ZX_DEVICE_NAME_MAX];
  if (vid_ == PDEV_VID_GENERIC && pid_ == PDEV_PID_GENERIC && did_ == PDEV_DID_KPCI) {
    strlcpy(name, "pci", sizeof(name));
  } else {
    snprintf(name, sizeof(name), "%02x:%02x:%01x", vid_, pid_, did_);
  }
  char argstr[64];
  snprintf(argstr, sizeof(argstr), "pdev:%s,", name);

  uint32_t device_add_flags = 0;

  // Isolated devices run in separate devhosts.
  // Protocol devices must be in same devhost as platform bus.
  // Composite device fragments are also in the same devhost as platform bus,
  // but the actual composite device will be in a new devhost or devhost belonging to
  // one of the other fragments.
  if (type_ == Isolated) {
    device_add_flags |= DEVICE_ADD_MUST_ISOLATE;
  }

  const size_t metadata_count = resources_.metadata_count();
  const size_t boot_metadata_count = resources_.boot_metadata_count();
  if (metadata_count > 0 || boot_metadata_count > 0) {
    // Keep device invisible until after we add its metadata.
    device_add_flags |= DEVICE_ADD_INVISIBLE;
  }

  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, vid_},
      {BIND_PLATFORM_DEV_PID, 0, pid_},
      {BIND_PLATFORM_DEV_DID, 0, did_},
  };
  zx_status_t status = DdkAdd(name, device_add_flags, props, fbl::count_of(props), ZX_PROTOCOL_PDEV,
                              (type_ == Isolated ? argstr : nullptr));
  if (status != ZX_OK) {
    return status;
  }

  if (metadata_count > 0 || boot_metadata_count > 0) {
    for (size_t i = 0; i < metadata_count; i++) {
      const auto& metadata = resources_.metadata(i);
      status = DdkAddMetadata(metadata.type, metadata.data_buffer, metadata.data_size);
      if (status != ZX_OK) {
        DdkAsyncRemove();
        return status;
      }
    }

    for (size_t i = 0; i < boot_metadata_count; i++) {
      const auto& metadata = resources_.boot_metadata(i);
      fbl::Array<uint8_t> data;
      status = bus_->GetBootItem(metadata.zbi_type, metadata.zbi_extra, &data);
      if (status == ZX_OK) {
        status = DdkAddMetadata(metadata.zbi_type, data.data(), data.size());
      }
      if (status != ZX_OK) {
        zxlogf(WARN, "%s failed to add metadata for new device\n", __func__);
      }
    }

    DdkMakeVisible();
  }

  return ZX_OK;
}

}  // namespace platform_bus
