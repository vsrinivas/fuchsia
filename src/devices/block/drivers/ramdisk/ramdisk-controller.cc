// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/ramdisk/c/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <memory>
#include <string>

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <ddktl/device.h>

#include "ramdisk.h"

namespace ramdisk {
namespace {

class RamdiskController;
using RamdiskControllerDeviceType = ddk::Device<RamdiskController, ddk::Messageable>;

class RamdiskController : public RamdiskControllerDeviceType {
 public:
  RamdiskController(zx_device_t* parent) : RamdiskControllerDeviceType(parent) {}

  // Device Protocol
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease() { delete this; }

 private:
  // FIDL Interface RamdiskController.
  zx_status_t Create(uint64_t block_size, uint64_t block_count,
                     const fuchsia_hardware_ramdisk_GUID* type_guid, fidl_txn_t* txn);
  zx_status_t CreateFromVmo(zx_handle_t vmo, fidl_txn_t* txn);
  zx_status_t FidlCreateFromVmoWithBlockSize(zx_handle_t vmo, uint64_t block_size, fidl_txn_t* txn);

  // Other methods:
  // ConfigureDevice returns the name of the device if successful.
  zx::status<std::string> ConfigureDevice(zx::vmo vmo, uint64_t block_size, uint64_t block_count,
                                          const uint8_t* type_guid);

  zx::status<std::string> CreateFromVmoWithBlockSize(zx::vmo vmo, uint64_t block_size);

  static const fuchsia_hardware_ramdisk_RamdiskController_ops* Ops() {
    using Binder = fidl::Binder<RamdiskController>;

    static const fuchsia_hardware_ramdisk_RamdiskController_ops kOps = {
        .Create = Binder::BindMember<&RamdiskController::Create>,
        .CreateFromVmo = Binder::BindMember<&RamdiskController::CreateFromVmo>,
        .CreateFromVmoWithBlockSize =
            Binder::BindMember<&RamdiskController::FidlCreateFromVmoWithBlockSize>,
    };
    return &kOps;
  }
};

zx_status_t RamdiskController::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_ramdisk_RamdiskController_dispatch(this, txn, msg, Ops());
}

zx_status_t RamdiskController::Create(uint64_t block_size, uint64_t block_count,
                                      const fuchsia_hardware_ramdisk_GUID* type_guid,
                                      fidl_txn_t* txn) {
  auto failure_response = [&txn](zx_status_t status) -> zx_status_t {
    return fuchsia_hardware_ramdisk_RamdiskControllerCreate_reply(txn, status, nullptr, 0);
  };

  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(block_size * block_count, ZX_VMO_RESIZABLE, &vmo);
  if (status != ZX_OK) {
    return failure_response(status);
  }
  auto name_or = ConfigureDevice(std::move(vmo), block_size, block_count,
                                 type_guid ? type_guid->value : nullptr);
  if (name_or.is_error()) {
    return failure_response(status);
  }
  return fuchsia_hardware_ramdisk_RamdiskControllerCreate_reply(txn, ZX_OK, name_or.value().data(),
                                                                name_or.value().length());
}

zx::status<std::string> RamdiskController::CreateFromVmoWithBlockSize(zx::vmo vmo,
                                                                      uint64_t block_size) {
  // Ensure this is the last handle to this VMO; otherwise, the size
  // may change from underneath us.
  zx_info_handle_count_t info;
  zx_status_t status = vmo.get_info(ZX_INFO_HANDLE_COUNT, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK || info.handle_count != 1) {
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

zx_status_t RamdiskController::CreateFromVmo(zx_handle_t vmo, fidl_txn_t* txn) {
  auto name_or = CreateFromVmoWithBlockSize(zx::vmo(vmo), PAGE_SIZE);
  if (name_or.is_error()) {
    return fuchsia_hardware_ramdisk_RamdiskControllerCreateFromVmo_reply(
        txn, name_or.status_value(), nullptr, 0);
  }

  return fuchsia_hardware_ramdisk_RamdiskControllerCreateFromVmo_reply(
      txn, ZX_OK, name_or.value().data(), name_or.value().length());
}

zx_status_t RamdiskController::FidlCreateFromVmoWithBlockSize(zx_handle_t vmo, uint64_t block_size,
                                                              fidl_txn_t* txn) {
  auto name_or = CreateFromVmoWithBlockSize(zx::vmo(vmo), block_size);
  if (name_or.is_error()) {
    return fuchsia_hardware_ramdisk_RamdiskControllerCreateFromVmoWithBlockSize_reply(
        txn, name_or.status_value(), nullptr, 0);
  }

  return fuchsia_hardware_ramdisk_RamdiskControllerCreateFromVmoWithBlockSize_reply(
      txn, ZX_OK, name_or.value().data(), name_or.value().length());
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

ZIRCON_DRIVER_BEGIN(ramdisk, ramdisk::ramdisk_driver_ops, "zircon", "0.1", 1)
BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT), ZIRCON_DRIVER_END(ramdisk)
