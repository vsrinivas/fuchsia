// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ram-nand-ctl.h"

#include <fuchsia/hardware/nand/c/fidl.h>
#include <inttypes.h>
#include <lib/zx/vmo.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/types.h>

#include <memory>

#include <ddktl/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/macros.h>

#include "ram-nand.h"

namespace {

class RamNandCtl;
using RamNandCtlDeviceType = ddk::Device<RamNandCtl, ddk::Messageable>;

class RamNandCtl : public RamNandCtlDeviceType {
 public:
  explicit RamNandCtl(zx_device_t* parent) : RamNandCtlDeviceType(parent) {}

  zx_status_t Bind() { return DdkAdd("nand-ctl"); }
  void DdkRelease() { delete this; }

  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

  zx_status_t CreateDevice(const fuchsia_hardware_nand_RamNandInfo* info, const char** name);

  DISALLOW_COPY_ASSIGN_AND_MOVE(RamNandCtl);
};

zx_status_t CreateDevice(void* ctx, const fuchsia_hardware_nand_RamNandInfo* info,
                         fidl_txn_t* txn) {
  RamNandCtl* device = reinterpret_cast<RamNandCtl*>(ctx);
  const char* name = nullptr;
  zx_status_t status = device->CreateDevice(info, &name);
  return fuchsia_hardware_nand_RamNandCtlCreateDevice_reply(txn, status, name,
                                                            name ? strlen(name) : 0);
}

fuchsia_hardware_nand_RamNandCtl_ops_t fidl_ops = {.CreateDevice = CreateDevice};

zx_status_t RamNandCtl::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_nand_RamNandCtl_dispatch(this, txn, msg, &fidl_ops);
}

zx_status_t RamNandCtl::CreateDevice(const fuchsia_hardware_nand_RamNandInfo* info,
                                     const char** name) {
  const auto& params = static_cast<const NandParams>(info->nand_info);
  fbl::AllocChecker checker;
  std::unique_ptr<NandDevice> device(new (&checker) NandDevice(params, zxdev()));
  if (!checker.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = device->Bind(*info);
  if (status != ZX_OK) {
    return status;
  }
  *name = device->name();

  // devmgr is now in charge of the device.
  __UNUSED NandDevice* dummy = device.release();
  return status;
}

}  // namespace

zx_status_t RamNandDriverBind(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker checker;
  std::unique_ptr<RamNandCtl> device(new (&checker) RamNandCtl(parent));
  if (!checker.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = device->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the device.
    __UNUSED RamNandCtl* dummy = device.release();
  }
  return status;
}
