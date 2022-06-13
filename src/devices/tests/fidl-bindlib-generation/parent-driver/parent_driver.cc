// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/fidl-bindlib-generation/parent-driver/parent_driver.h"

#include <bind/fuchsia/test/cpp/bind.h>
#include <bind/fuchsia/tools/bindc/test/cpp/bind.h>

#include "src/devices/tests/fidl-bindlib-generation/parent-driver/parent_driver-bind.h"

namespace parent_driver {

zx_status_t ParentDriver::Bind(void* ctx, zx_device_t* dev) {
  zx_device_prop_t deprecated_props[] = {
      {BIND_PROTOCOL, 0, bind_fuchsia_test::BIND_PROTOCOL_DEVICE},
  };
  zx_device_str_prop_t props[] = {
      {bind_fuchsia_tools_bindc_test::ECHO.c_str(),
       str_prop_enum_val(bind_fuchsia_tools_bindc_test::ECHO_BANJO.c_str())}};

  auto device = std::make_unique<ParentDriver>(dev);
  device->is_bound.Set(true);

  auto child_args = ddk::DeviceAddArgs("fidl_bindlib_generation")
                        .set_str_props(props)
                        .set_props(deprecated_props)
                        .set_inspect_vmo(device->inspect_vmo());
  zx_status_t status = device->DdkAdd(child_args);
  if (status != ZX_OK) {
    return status;
  }

  __UNUSED auto ptr = device.release();
  return ZX_OK;
}

void ParentDriver::DdkRelease() { delete this; }

static zx_driver_ops_t parent_driver_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = ParentDriver::Bind;
  return ops;
}();

}  // namespace parent_driver

ZIRCON_DRIVER(ParentDriver, parent_driver::parent_driver_driver_ops, "zircon", "0.1");
