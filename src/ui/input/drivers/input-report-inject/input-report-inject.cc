// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "input-report-inject.h"

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

namespace input_report_inject {

void InputReportInject::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

zx_status_t input_report_bind(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;

  auto dev = fbl::make_unique_checked<InputReportInject>(&ac, parent);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

zx_status_t InputReportInject::Bind() { return DdkAdd("InputReportInject"); }

zx_status_t InputReportInject::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
  auto inst = std::make_unique<InputReportInjectInstance>(zxdev());
  zx_status_t status = inst->Bind(this);
  if (status != ZX_OK) {
    return status;
  }

  {
    fbl::AutoLock lock(&instance_lock_);
    instance_list_.push_front(inst.get());
  }
  *dev_out = inst->zxdev();

  // devmgr is now in charge of the memory for inst.
  __UNUSED auto ptr = inst.release();
  return ZX_OK;
}

void InputReportInject::RemoveInstanceFromList(InputReportInjectInstance* instance) {
  fbl::AutoLock lock(&instance_lock_);

  for (auto& iter : instance_list_) {
    if (iter.zxdev() == instance->zxdev()) {
      instance_list_.erase(iter);
      break;
    }
  }
}

static zx_driver_ops_t input_report_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = input_report_bind;
  return ops;
}();

}  // namespace input_report_inject

// clang-format off
ZIRCON_DRIVER_BEGIN(InputReport, input_report_inject::input_report_driver_ops, "zircon", "0.1", 1)
   BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(inputReport)
    // clang-format on
