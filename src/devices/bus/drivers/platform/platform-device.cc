// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bus/drivers/platform/platform-device.h"

#include <assert.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/natural_types.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fit/function.h>
#include <lib/zircon-internal/align.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/syscalls/resource.h>

#include "src/devices/bus/drivers/platform/node-util.h"
#include "src/devices/bus/drivers/platform/platform-bus.h"

namespace platform_bus {

namespace fpbus = fuchsia_hardware_platform_bus;

// fuchsia.hardware.platform.bus.PlatformBus implementation.
void RestrictPlatformBus::NodeAdd(NodeAddRequestView request, fdf::Arena& arena,
                                  NodeAddCompleter::Sync& completer) {
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}
void RestrictPlatformBus::ProtocolNodeAdd(ProtocolNodeAddRequestView request, fdf::Arena& arena,
                                          ProtocolNodeAddCompleter::Sync& completer) {
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}
void RestrictPlatformBus::RegisterProtocol(RegisterProtocolRequestView request, fdf::Arena& arena,
                                           RegisterProtocolCompleter::Sync& completer) {
  upstream_->RegisterProtocol(request, arena, completer);
}

void RestrictPlatformBus::GetBoardInfo(fdf::Arena& arena, GetBoardInfoCompleter::Sync& completer) {
  upstream_->GetBoardInfo(arena, completer);
}
void RestrictPlatformBus::SetBoardInfo(SetBoardInfoRequestView request, fdf::Arena& arena,
                                       SetBoardInfoCompleter::Sync& completer) {
  upstream_->SetBoardInfo(request, arena, completer);
}
void RestrictPlatformBus::SetBootloaderInfo(SetBootloaderInfoRequestView request, fdf::Arena& arena,
                                            SetBootloaderInfoCompleter::Sync& completer) {
  upstream_->SetBootloaderInfo(request, arena, completer);
}

void RestrictPlatformBus::RegisterSysSuspendCallback(
    RegisterSysSuspendCallbackRequestView request, fdf::Arena& arena,
    RegisterSysSuspendCallbackCompleter::Sync& completer) {
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}
void RestrictPlatformBus::AddComposite(AddCompositeRequestView request, fdf::Arena& arena,
                                       AddCompositeCompleter::Sync& completer) {
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void RestrictPlatformBus::AddCompositeImplicitPbusFragment(
    AddCompositeImplicitPbusFragmentRequestView request, fdf::Arena& arena,
    AddCompositeImplicitPbusFragmentCompleter::Sync& completer) {
  completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
}

zx_status_t PlatformDevice::Create(fpbus::Node node, zx_device_t* parent, PlatformBus* bus,
                                   Type type, std::unique_ptr<platform_bus::PlatformDevice>* out) {
  fbl::AllocChecker ac;
  std::unique_ptr<platform_bus::PlatformDevice> dev(
      new (&ac) platform_bus::PlatformDevice(parent, bus, type, node));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Init();
  if (status != ZX_OK) {
    return status;
  }
  out->swap(dev);
  return ZX_OK;
}

PlatformDevice::PlatformDevice(zx_device_t* parent, PlatformBus* bus, Type type, fpbus::Node node)
    : PlatformDeviceType(parent),
      bus_(bus),
      type_(type),
      vid_(node.vid().value_or(0)),
      pid_(node.pid().value_or(0)),
      did_(node.did().value_or(0)),
      instance_id_(node.instance_id().value_or(0)),
      node_(std::move(node)),
      outgoing_(driver::OutgoingDirectory::Create(fdf::Dispatcher::GetCurrent()->get())) {
  strlcpy(name_, node.name().value_or("no name?").data(), sizeof(name_));
}

zx_status_t PlatformDevice::Init() {
  if (type_ == Protocol) {
    // Protocol devices implement a subset of the platform bus protocol.
    restricted_ = std::make_unique<RestrictPlatformBus>(bus_);
  }

  return ZX_OK;
}

zx_status_t PlatformDevice::PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) {
  if (node_.mmio() == std::nullopt || index >= node_.mmio()->size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const auto& mmio = node_.mmio().value()[index];
  if (unlikely(!IsValid(mmio))) {
    return ZX_ERR_INTERNAL;
  }
  if (mmio.base() == std::nullopt) {
    return ZX_ERR_NOT_FOUND;
  }
  const zx_paddr_t vmo_base = ZX_ROUNDDOWN(mmio.base().value(), ZX_PAGE_SIZE);
  const size_t vmo_size =
      ZX_ROUNDUP(mmio.base().value() + mmio.length().value() - vmo_base, ZX_PAGE_SIZE);
  zx::vmo vmo;

  zx_status_t status = zx::vmo::create_physical(*bus_->GetResource(), vmo_base, vmo_size, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: creating vmo failed %d", __FUNCTION__, status);
    return status;
  }

  char name[32];
  snprintf(name, sizeof(name), "mmio %u", index);
  status = vmo.set_property(ZX_PROP_NAME, name, sizeof(name));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: setting vmo name failed %d", __FUNCTION__, status);
    return status;
  }

  out_mmio->offset = mmio.base().value() - vmo_base;
  out_mmio->vmo = vmo.release();
  out_mmio->size = mmio.length().value();
  return ZX_OK;
}

zx_status_t PlatformDevice::PDevGetInterrupt(uint32_t index, uint32_t flags,
                                             zx::interrupt* out_irq) {
  if (node_.irq() == std::nullopt || index >= node_.irq()->size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (out_irq == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  const auto& irq = node_.irq().value()[index];
  if (unlikely(!IsValid(irq))) {
    return ZX_ERR_INTERNAL;
  }
  if (flags == 0) {
    flags = irq.mode().value();
  }
  zx_status_t status =
      zx::interrupt::create(*bus_->GetResource(), irq.irq().value(), flags, out_irq);
  if (status != ZX_OK) {
    zxlogf(ERROR, "platform_dev_map_interrupt: zx_interrupt_create failed %d", status);
    return status;
  }
  return status;
}

zx_status_t PlatformDevice::PDevGetBti(uint32_t index, zx::bti* out_bti) {
  if (node_.bti() == std::nullopt || index >= node_.bti()->size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (out_bti == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  const auto& bti = node_.bti().value()[index];
  if (unlikely(!IsValid(bti))) {
    return ZX_ERR_INTERNAL;
  }

  return bus_->IommuGetBti(bti.iommu_index().value(), bti.bti_id().value(), out_bti);
}

zx_status_t PlatformDevice::PDevGetSmc(uint32_t index, zx::resource* out_resource) {
  if (node_.smc() == std::nullopt || index >= node_.smc()->size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (out_resource == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  const auto& smc = node_.smc().value()[index];
  if (unlikely(!IsValid(smc))) {
    return ZX_ERR_INTERNAL;
  }

  uint32_t options = ZX_RSRC_KIND_SMC;
  if (smc.exclusive().value())
    options |= ZX_RSRC_FLAG_EXCLUSIVE;
  char rsrc_name[ZX_MAX_NAME_LEN];
  snprintf(rsrc_name, ZX_MAX_NAME_LEN - 1, "%s.pbus[%u]", name_, index);
  return zx::resource::create(*bus_->GetResource(), options, smc.service_call_num_base().value(),
                              smc.count().value(), rsrc_name, sizeof(rsrc_name), out_resource);
}

zx_status_t PlatformDevice::PDevGetDeviceInfo(pdev_device_info_t* out_info) {
  pdev_device_info_t info = {
      .vid = vid_,
      .pid = pid_,
      .did = did_,
      .mmio_count = static_cast<uint32_t>(node_.mmio().has_value() ? node_.mmio()->size() : 0),
      .irq_count = static_cast<uint32_t>(node_.irq().has_value() ? node_.irq()->size() : 0),
      .bti_count = static_cast<uint32_t>(node_.bti().has_value() ? node_.bti()->size() : 0),
      .smc_count = static_cast<uint32_t>(node_.smc().has_value() ? node_.smc()->size() : 0),
      .metadata_count =
          static_cast<uint32_t>(node_.metadata().has_value() ? node_.metadata()->size() : 0),
      .reserved = {},
      .name = {},
  };
  static_assert(sizeof(info.name) == sizeof(name_), "");
  memcpy(info.name, name_, sizeof(out_info->name));
  memcpy(out_info, &info, sizeof(info));

  return ZX_OK;
}

zx_status_t PlatformDevice::PDevGetBoardInfo(pdev_board_info_t* out_info) {
  auto info = bus_->board_info();
  out_info->pid = info.pid();
  out_info->vid = info.vid();
  out_info->board_revision = info.board_revision();
  strlcpy(out_info->board_name, info.board_name().data(), sizeof(out_info->board_name));
  return ZX_OK;
}

zx_status_t PlatformDevice::PDevDeviceAdd(uint32_t index, const device_add_args_t* args,
                                          zx_device_t** device) {
  return ZX_ERR_NOT_SUPPORTED;
}

// Create a resource and pass it back to the proxy along with necessary metadata
// to create/map the VMO in the driver process.
zx_status_t PlatformDevice::RpcGetMmio(uint32_t index, zx_paddr_t* out_paddr, size_t* out_length,
                                       zx_handle_t* out_handle, uint32_t* out_handle_count) {
  if (node_.mmio() == std::nullopt || index >= node_.mmio()->size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  const auto& root_rsrc = bus_->GetResource();
  if (!root_rsrc->is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }

  const auto& mmio = node_.mmio().value()[index];
  if (unlikely(!IsValid(mmio))) {
    return ZX_ERR_INTERNAL;
  }
  zx::resource resource;
  char rsrc_name[ZX_MAX_NAME_LEN];
  snprintf(rsrc_name, ZX_MAX_NAME_LEN - 1, "%s.pbus[%u]", name_, index);
  zx_status_t status =
      zx::resource::create(*root_rsrc, ZX_RSRC_KIND_MMIO, mmio.base().value(),
                           mmio.length().value(), rsrc_name, sizeof(rsrc_name), &resource);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_rpc_get_mmio: zx_resource_create failed: %d", name_, status);
    return status;
  }

  *out_paddr = mmio.base().value();
  *out_length = mmio.length().value();
  *out_handle_count = 1;
  *out_handle = resource.release();
  return ZX_OK;
}

// Create a resource and pass it back to the proxy along with necessary metadata
// to create the IRQ in the driver process.
zx_status_t PlatformDevice::RpcGetInterrupt(uint32_t index, uint32_t* out_irq, uint32_t* out_mode,
                                            zx_handle_t* out_handle, uint32_t* out_handle_count) {
  if (node_.irq() == std::nullopt || index >= node_.irq()->size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const auto& root_rsrc = bus_->GetResource();
  if (!root_rsrc->is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }

  zx::resource resource;
  const auto& irq = node_.irq().value()[index];
  if (unlikely(!IsValid(irq))) {
    return ZX_ERR_INTERNAL;
  }
  uint32_t options = ZX_RSRC_KIND_IRQ | ZX_RSRC_FLAG_EXCLUSIVE;
  char rsrc_name[ZX_MAX_NAME_LEN];
  snprintf(rsrc_name, ZX_MAX_NAME_LEN - 1, "%s.pbus[%u]", name_, index);
  zx_status_t status = zx::resource::create(*root_rsrc, options, irq.irq().value(), 1, rsrc_name,
                                            sizeof(rsrc_name), &resource);
  if (status != ZX_OK) {
    return status;
  }

  *out_irq = irq.irq().value();
  *out_mode = irq.mode().value();
  *out_handle_count = 1;
  *out_handle = resource.release();
  return ZX_OK;
}

zx_status_t PlatformDevice::RpcGetBti(uint32_t index, zx_handle_t* out_handle,
                                      uint32_t* out_handle_count) {
  if (node_.bti() == std::nullopt || index >= node_.bti()->size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const auto& bti = node_.bti().value()[index];
  if (unlikely(!IsValid(bti))) {
    return ZX_ERR_INTERNAL;
  }

  zx::bti out_bti;
  zx_status_t status = bus_->IommuGetBti(bti.iommu_index().value(), bti.bti_id().value(), &out_bti);
  *out_handle = out_bti.release();

  if (status == ZX_OK) {
    *out_handle_count = 1;
  }

  return status;
}

zx_status_t PlatformDevice::RpcGetSmc(uint32_t index, zx_handle_t* out_handle,
                                      uint32_t* out_handle_count) {
  if (node_.smc() == std::nullopt || index >= node_.smc()->size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const auto& root_rsrc = bus_->GetResource();
  if (!root_rsrc->is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }

  zx::resource resource;
  const auto& smc = node_.smc().value()[index];
  if (unlikely(!IsValid(smc))) {
    return ZX_ERR_INTERNAL;
  }
  uint32_t options = ZX_RSRC_KIND_SMC;
  if (smc.exclusive().value())
    options |= ZX_RSRC_FLAG_EXCLUSIVE;
  char rsrc_name[ZX_MAX_NAME_LEN];
  snprintf(rsrc_name, ZX_MAX_NAME_LEN - 1, "%s.pbus[%u]", name_, index);
  zx_status_t status =
      zx::resource::create(*root_rsrc, options, smc.service_call_num_base().value(),
                           smc.count().value(), rsrc_name, sizeof(rsrc_name), &resource);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_rpc_get_smc: zx_resource_create failed: %d", name_, status);
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
      .mmio_count = static_cast<uint32_t>(node_.mmio().has_value() ? node_.mmio()->size() : 0),
      .irq_count = static_cast<uint32_t>(node_.irq().has_value() ? node_.irq()->size() : 0),
      .bti_count = static_cast<uint32_t>(node_.bti().has_value() ? node_.bti()->size() : 0),
      .smc_count = static_cast<uint32_t>(node_.smc().has_value() ? node_.smc()->size() : 0),
      .metadata_count =
          static_cast<uint32_t>(node_.metadata().has_value() ? node_.metadata()->size() : 0),

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
  size_t normal_metadata = (node_.metadata() == std::nullopt ? 0 : node_.metadata()->size());
  size_t max_metadata =
      normal_metadata + (node_.boot_metadata() == std::nullopt ? 0 : node_.boot_metadata()->size());
  if (index >= max_metadata) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (index < normal_metadata) {
    const auto& metadata = node_.metadata().value()[index];
    if (unlikely(!IsValid(metadata))) {
      return ZX_ERR_INTERNAL;
    }
    if (metadata.data()->size() > buf_size) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(buf, metadata.data()->data(), metadata.data()->size());
    *out_type = *metadata.type();
    *actual = static_cast<uint32_t>(metadata.data()->size());
    return ZX_OK;
  }

  // boot_metadata indices follow metadata indices.
  index -= static_cast<uint32_t>(normal_metadata);

  const auto& metadata = node_.boot_metadata().value()[index];
  if (!IsValid(metadata)) {
    return ZX_ERR_INTERNAL;
  }
  zx::status metadata_bi =
      bus_->GetBootItem(metadata.zbi_type().value(), metadata.zbi_extra().value());
  if (metadata_bi.is_error()) {
    return metadata_bi.status_value();
  } else if (metadata_bi->length > buf_size) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  auto& [vmo, length] = *metadata_bi;
  auto status = vmo.read(buf, 0, length);
  if (status != ZX_OK) {
    return status;
  }
  *out_type = metadata.zbi_type().value();
  *actual = length;
  return ZX_OK;
}

zx_status_t PlatformDevice::DdkGetProtocol(uint32_t proto_id, void* out) {
  if (proto_id == ZX_PROTOCOL_PDEV) {
    auto proto = static_cast<pdev_protocol_t*>(out);
    proto->ops = &pdev_protocol_ops_;
    proto->ctx = this;
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
                                std::size(req_handles), &actual, &req_handle_count);
  if (status != ZX_OK) {
    zxlogf(ERROR, "platform_dev_rxrpc: zx_channel_read failed %d", status);
    return status;
  }

  resp_header->txid = req_header->txid;
  uint32_t resp_len;

  auto req = reinterpret_cast<rpc_pdev_req_t*>(&req_buf);
  if (actual < sizeof(*req)) {
    zxlogf(ERROR, "%s received %u, expecting %zu (PDEV)", __func__, actual, sizeof(*req));
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
      status = PDevGetBoardInfo(&resp->board_info);
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
      zxlogf(ERROR, "%s: unknown pdev op %u", __func__, req_header->op);
      return ZX_ERR_INTERNAL;
  }

  // set op to match request so zx_channel_write will return our response
  resp_header->status = status;
  status = zx_channel_write(channel, 0, resp_header, resp_len,
                            (resp_handle_count ? resp_handles : nullptr), resp_handle_count);
  if (status != ZX_OK) {
    zxlogf(ERROR, "platform_dev_rxrpc: zx_channel_write failed %d", status);
  }
  return status;
}

void PlatformDevice::DdkRelease() { delete this; }

zx_status_t PlatformDevice::Start() {
  char name[ZX_DEVICE_NAME_MAX];
  if (vid_ == PDEV_VID_GENERIC && pid_ == PDEV_PID_GENERIC && did_ == PDEV_DID_KPCI) {
    strlcpy(name, "pci", sizeof(name));
  } else {
    if (instance_id_ == 0) {
      // For backwards compatability, we elide instance id when it is 0.
      snprintf(name, sizeof(name), "%02x:%02x:%01x", vid_, pid_, did_);
    } else {
      snprintf(name, sizeof(name), "%02x:%02x:%01x:%01x", vid_, pid_, did_, instance_id_);
    }
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

  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, vid_},
      {BIND_PLATFORM_DEV_PID, 0, pid_},
      {BIND_PLATFORM_DEV_DID, 0, did_},
      {BIND_PLATFORM_DEV_INSTANCE_ID, 0, instance_id_},
  };

  ddk::DeviceAddArgs args(name);
  args.set_flags(device_add_flags)
      .set_props(props)
      .set_proto_id(ZX_PROTOCOL_PDEV)
      .set_proxy_args((type_ == Isolated ? argstr : nullptr));

  std::array protocol_offers = {
      fuchsia_hardware_platform_bus::Service::Name,
  };

  if (type_ == Protocol) {
    driver::ServiceInstanceHandler handler;
    fuchsia_hardware_platform_bus::Service::Handler service(&handler);

    auto protocol = [this](fdf::ServerEnd<fuchsia_hardware_platform_bus::PlatformBus> server_end) {
      fdf::BindServer<fdf::WireServer<fuchsia_hardware_platform_bus::PlatformBus>>(
          fdf::Dispatcher::GetCurrent()->get(), std::move(server_end), restricted_.get());
    };

    auto status = service.add_platform_bus(std::move(protocol));
    if (status.is_error()) {
      return status.error_value();
    }

    status = outgoing_.AddService<fuchsia_hardware_platform_bus::Service>(std::move(handler));
    if (status.is_error()) {
      return status.error_value();
    }

    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (endpoints.is_error()) {
      return endpoints.status_value();
    }

    auto result = outgoing_.Serve(std::move(endpoints->server));
    if (result.is_error()) {
      return result.error_value();
    }

    args.set_outgoing_dir(endpoints->client.TakeChannel())
        .set_runtime_service_offers(protocol_offers);
  }
  return DdkAdd(std::move(args));
}

void PlatformDevice::DdkInit(ddk::InitTxn txn) {
  const size_t metadata_count = node_.metadata() == std::nullopt ? 0 : node_.metadata()->size();
  const size_t boot_metadata_count =
      node_.boot_metadata() == std::nullopt ? 0 : node_.boot_metadata()->size();
  if (metadata_count > 0 || boot_metadata_count > 0) {
    for (size_t i = 0; i < metadata_count; i++) {
      const auto& metadata = node_.metadata().value()[i];
      if (!IsValid(metadata)) {
        txn.Reply(ZX_ERR_INTERNAL);
        return;
      }
      zx_status_t status =
          DdkAddMetadata(metadata.type().value(), metadata.data()->data(), metadata.data()->size());
      if (status != ZX_OK) {
        return txn.Reply(status);
      }
    }

    for (size_t i = 0; i < boot_metadata_count; i++) {
      const auto& metadata = node_.boot_metadata().value()[i];
      if (!IsValid(metadata)) {
        txn.Reply(ZX_ERR_INTERNAL);
        return;
      }
      zx::status data =
          bus_->GetBootItemArray(metadata.zbi_type().value(), metadata.zbi_extra().value());
      zx_status_t status = data.status_value();
      if (data.is_ok()) {
        status = DdkAddMetadata(metadata.zbi_type().value(), data->data(), data->size());
      }
      if (status != ZX_OK) {
        zxlogf(WARNING, "%s failed to add metadata for new device", __func__);
      }
    }
  }
  return txn.Reply(ZX_OK);
}

}  // namespace platform_bus
