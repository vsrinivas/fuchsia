// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ram-nand-ctl.h"

#include <fuchsia/hardware/nand/llcpp/fidl.h>
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
#include "src/devices/lib/nand/nand.h"

namespace {

class RamNandCtl;
using RamNandCtlDeviceType =
    ddk::Device<RamNandCtl, ddk::Messageable<fuchsia_hardware_nand::RamNandCtl>::Mixin>;

class RamNandCtl : public RamNandCtlDeviceType,
                   public fidl::WireServer<fuchsia_hardware_nand::RamNandCtl> {
 public:
  explicit RamNandCtl(zx_device_t* parent) : RamNandCtlDeviceType(parent) {}

  zx_status_t Bind() { return DdkAdd("nand-ctl"); }
  void DdkRelease() { delete this; }

  void CreateDevice(CreateDeviceRequestView request, CreateDeviceCompleter::Sync& completer);

  DISALLOW_COPY_ASSIGN_AND_MOVE(RamNandCtl);
};

void RamNandCtl::CreateDevice(CreateDeviceRequestView request,
                              CreateDeviceCompleter::Sync& completer) {
  nand_info_t temp_info;
  nand::nand_banjo_from_fidl(request->info.nand_info, &temp_info);
  const auto& params = static_cast<const NandParams>(temp_info);
  fbl::AllocChecker checker;
  std::unique_ptr<NandDevice> device(new (&checker) NandDevice(params, zxdev()));
  if (!checker.check()) {
    completer.Reply(ZX_ERR_NO_MEMORY, fidl::StringView());
    return;
  }

  zx_status_t status = device->Bind(request->info);
  if (status != ZX_OK) {
    completer.Reply(status, fidl::StringView());
    return;
  }

  // devmgr is now in charge of the device.
  __UNUSED NandDevice* dummy = device.release();
  completer.Reply(ZX_OK, fidl::StringView::FromExternal(dummy->name(), strlen(dummy->name())));
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
