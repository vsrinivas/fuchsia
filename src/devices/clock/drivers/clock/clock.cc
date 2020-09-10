// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "clock.h"

#include <zircon/types.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/clock.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>

zx_status_t ClockDevice::ClockEnable() { return clock_.Enable(id_); }

zx_status_t ClockDevice::ClockDisable() { return clock_.Disable(id_); }

zx_status_t ClockDevice::ClockIsEnabled(bool* out_enabled) {
  return clock_.IsEnabled(id_, out_enabled);
}

zx_status_t ClockDevice::ClockSetRate(uint64_t hz) { return clock_.SetRate(id_, hz); }

zx_status_t ClockDevice::ClockQuerySupportedRate(uint64_t max_rate,
                                                 uint64_t* out_max_supported_rate) {
  return clock_.QuerySupportedRate(id_, max_rate, out_max_supported_rate);
}

zx_status_t ClockDevice::ClockSetInput(uint32_t idx) { return clock_.SetInput(id_, idx); }

zx_status_t ClockDevice::ClockGetNumInputs(uint32_t* out) { return clock_.GetNumInputs(id_, out); }

zx_status_t ClockDevice::ClockGetInput(uint32_t* out) { return clock_.GetInput(id_, out); }

zx_status_t ClockDevice::ClockGetRate(uint64_t* out_current_rate) {
  return clock_.GetRate(id_, out_current_rate);
}

void ClockDevice::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void ClockDevice::DdkRelease() { delete this; }

zx_status_t ClockDevice::Create(void* ctx, zx_device_t* parent) {
  clock_impl_protocol_t clock_proto;
  auto status = device_get_protocol(parent, ZX_PROTOCOL_CLOCK_IMPL, &clock_proto);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_get_protocol failed %d", __FILE__, status);
    return status;
  }

  size_t metadata_size;
  status = device_get_metadata_size(parent, DEVICE_METADATA_CLOCK_IDS, &metadata_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_get_metadata_size failed %d", __FILE__, status);
    return status;
  }
  auto clock_count = metadata_size / sizeof(clock_id_t);

  fbl::AllocChecker ac;
  std::unique_ptr<clock_id_t[]> clock_ids(new (&ac) clock_id_t[clock_count]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  size_t actual;
  status = device_get_metadata(parent, DEVICE_METADATA_CLOCK_IDS, clock_ids.get(), metadata_size,
                               &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_get_metadata failed %d", __FILE__, status);
    return status;
  }
  if (actual != metadata_size) {
    zxlogf(ERROR, "%s: device_get_metadata size error %d", __FILE__, status);
    return ZX_ERR_INTERNAL;
  }

  for (uint32_t i = 0; i < clock_count; i++) {
    auto clock_id = clock_ids[i].clock_id;
    fbl::AllocChecker ac;
    std::unique_ptr<ClockDevice> dev(new (&ac) ClockDevice(parent, &clock_proto, clock_id));
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    char name[20];
    snprintf(name, sizeof(name), "clock-%u", clock_id);
    zx_device_prop_t props[] = {
        {BIND_CLOCK_ID, 0, clock_id},
    };

    status = dev->DdkAdd(
        ddk::DeviceAddArgs(name).set_flags(DEVICE_ADD_ALLOW_MULTI_COMPOSITE).set_props(props));
    if (status != ZX_OK) {
      return status;
    }

    // dev is now owned by devmgr.
    __UNUSED auto ptr = dev.release();
  }

  return ZX_OK;
}

namespace {

constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = ClockDevice::Create;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER_BEGIN(clock, driver_ops, "zircon", "0.1", 1)
BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK_IMPL), ZIRCON_DRIVER_END(clock)
