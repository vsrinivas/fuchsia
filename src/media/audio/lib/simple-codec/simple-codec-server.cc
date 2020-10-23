// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/simple-codec/simple-codec-server.h>

#include <algorithm>
#include <memory>

#include <ddk/binding.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

namespace audio {

zx_status_t SimpleCodecServer::CreateInternal() {
  auto res = Initialize();
  if (res.is_error()) {
    return res.error_value();
  }
  loop_.StartThread();
  driver_ids_ = res.value();
  Info info = GetInfo();
  if (driver_ids_.instance_count != 0) {
    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, driver_ids_.vendor_id},
        {BIND_PLATFORM_DEV_DID, 0, driver_ids_.device_id},
        {BIND_CODEC_INSTANCE, 0, driver_ids_.instance_count},
    };
    return DdkAdd(ddk::DeviceAddArgs(info.product_name.c_str()).set_props(props));
  }
  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, driver_ids_.vendor_id},
      {BIND_PLATFORM_DEV_DID, 0, driver_ids_.device_id},
  };
  return DdkAdd(ddk::DeviceAddArgs(info.product_name.c_str()).set_props(props));
}

zx_status_t SimpleCodecServer::CodecConnect(zx::channel channel) {
  binding_.emplace(this, std::move(channel), loop_.dispatcher());
  return ZX_OK;
}

}  // namespace audio
