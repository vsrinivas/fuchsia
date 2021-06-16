// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/ramdisk/llcpp/fidl.h>
#include <lib/ddk/driver.h>
#include <lib/fidl-utils/bind.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <memory>
#include <string>

#include <ddktl/device.h>

#include "ramdisk.h"
#include "src/devices/block/drivers/ramdisk/ramdisk_bind.h"

namespace ramdisk {
namespace {

class RamdiskController;
using RamdiskControllerDeviceType =
    ddk::Device<RamdiskController,
                ddk::Messageable<fuchsia_hardware_ramdisk::RamdiskController>::Mixin>;

class RamdiskController : public RamdiskControllerDeviceType {
 public:
  RamdiskController(zx_device_t* parent) : RamdiskControllerDeviceType(parent) {}

  // Device Protocol
  void DdkRelease() { delete this; }

 private:
  // FIDL Interface RamdiskController.
  void Create(CreateRequestView request, CreateCompleter::Sync& completer);
  void CreateFromVmo(CreateFromVmoRequestView request, CreateFromVmoCompleter::Sync& completer);
  void CreateFromVmoWithBlockSize(CreateFromVmoWithBlockSizeRequestView request,
                                  CreateFromVmoWithBlockSizeCompleter::Sync& completer);

  // Other methods:
  // ConfigureDevice returns the name of the device if successful.
  zx::status<std::string> ConfigureDevice(zx::vmo vmo, uint64_t block_size, uint64_t block_count,
                                          const uint8_t* type_guid);

  zx::status<std::string> CreateFromVmoWithBlockSizeInternal(zx::vmo vmo, uint64_t block_size);
};

void RamdiskController::Create(CreateRequestView request, CreateCompleter::Sync& completer) {
  zx::vmo vmo;
  zx_status_t status =
      zx::vmo::create(request->block_size * request->block_count, ZX_VMO_RESIZABLE, &vmo);
  if (status != ZX_OK) {
    completer.Reply(status, fidl::StringView());
    return;
  }
  auto name_or = ConfigureDevice(std::move(vmo), request->block_size, request->block_count,
                                 request->type_guid ? request->type_guid->value.data() : nullptr);
  if (name_or.is_error()) {
    completer.Reply(status, fidl::StringView());
    return;
  }
  completer.Reply(ZX_OK, fidl::StringView::FromExternal(name_or.value()));
}

zx::status<std::string> RamdiskController::CreateFromVmoWithBlockSizeInternal(zx::vmo vmo,
                                                                              uint64_t block_size) {
  zx_info_handle_count_t handle_count_info;
  zx_status_t status = vmo.get_info(ZX_INFO_HANDLE_COUNT, &handle_count_info,
                                    sizeof(handle_count_info), nullptr, nullptr);
  if (status != ZX_OK) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  zx_info_vmo_t vmo_info;
  status = vmo.get_info(ZX_INFO_VMO, &vmo_info, sizeof(vmo_info), nullptr, nullptr);
  if (status != ZX_OK) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  // If this is a resizeable VMO, ensure it has only one handle to prevent
  // the size from changing underneath us.
  if ((vmo_info.flags & ZX_INFO_VMO_RESIZABLE) && (handle_count_info.handle_count != 1)) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  uint64_t vmo_size;
  status = vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  return ConfigureDevice(std::move(vmo), block_size, (vmo_size + block_size - 1) / block_size,
                         nullptr);
}

void RamdiskController::CreateFromVmo(CreateFromVmoRequestView request,
                                      CreateFromVmoCompleter::Sync& completer) {
  auto name_or =
      CreateFromVmoWithBlockSizeInternal(std::move(request->vmo), zx_system_get_page_size());
  if (name_or.is_error()) {
    completer.Reply(name_or.status_value(), fidl::StringView());
    return;
  }

  completer.Reply(ZX_OK, fidl::StringView::FromExternal(name_or.value()));
}

void RamdiskController::CreateFromVmoWithBlockSize(
    CreateFromVmoWithBlockSizeRequestView request,
    CreateFromVmoWithBlockSizeCompleter::Sync& completer) {
  auto name_or = CreateFromVmoWithBlockSizeInternal(std::move(request->vmo), request->block_size);
  if (name_or.is_error()) {
    completer.Reply(name_or.status_value(), fidl::StringView());
    return;
  }

  completer.Reply(ZX_OK, fidl::StringView::FromExternal(name_or.value()));
}

zx::status<std::string> RamdiskController::ConfigureDevice(zx::vmo vmo, uint64_t block_size,
                                                           uint64_t block_count,
                                                           const uint8_t* type_guid) {
  std::unique_ptr<Ramdisk> ramdev;
  zx_status_t status =
      Ramdisk::Create(zxdev(), std::move(vmo), block_size, block_count, type_guid, &ramdev);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  if ((status = ramdev->DdkAdd(ramdev->Name()) != ZX_OK)) {
    ramdev.release()->DdkRelease();
    return zx::error(status);
  }
  // RamdiskController owned by the DDK after being added successfully.
  return zx::ok(std::string(ramdev.release()->Name()));
}

zx_status_t RamdiskDriverBind(void* ctx, zx_device_t* parent) {
  auto ramctl = std::make_unique<RamdiskController>(parent);

  zx_status_t status = ramctl->DdkAdd("ramctl");
  if (status != ZX_OK) {
    return status;
  }

  // RamdiskController owned by the DDK after being added successfully.
  __UNUSED auto ptr = ramctl.release();
  return ZX_OK;
}

zx_driver_ops_t ramdisk_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = RamdiskDriverBind;
  return ops;
}();

}  // namespace
}  // namespace ramdisk

ZIRCON_DRIVER(ramdisk, ramdisk::ramdisk_driver_ops, "zircon", "0.1");
