// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>

#include <string>

#include <bind/bindlib/codegen/testlib/cpp/bind.h>
#include <bind/bindlibparent/codegen/testlib/cpp/bind.h>

#include "src/devices/tests/bindlib-codegen-test/parent-driver-bind.h"

namespace lib = bind_bindlib_codegen_testlib;
namespace parent = bind_bindlibparent_codegen_testlib;

static zx_device_t* dev = nullptr;

static void unbind(void* ctx) { device_unbind_reply(dev); }

static constexpr zx_protocol_device_t dev_ops = []() {
  zx_protocol_device_t ops = {};
  ops.version = DEVICE_OPS_VERSION;
  ops.unbind = unbind;
  return ops;
}();

static zx_status_t bind_func(void* ctx, zx_device_t* parent) {
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "parent";
  args.ops = &dev_ops;
  zx_device_prop_t props[] = {
      {BIND_PROTOCOL, 0, 3},
      {BIND_PCI_VID, 0, lib::BIND_PCI_VID_PIE},
      {BIND_PCI_DID, 0, 1234},
  };
  args.props = props;
  args.prop_count = std::size(props);

  zx_device_str_prop_t str_props[] = {
      zx_device_str_prop_t{.key = lib::KINGLET.c_str(),
                           .property_value = str_prop_str_val("firecrest")},
      zx_device_str_prop_t{.key = lib::MOON.c_str(),
                           .property_value = str_prop_enum_val(lib::MOON_HALF.c_str())},
      zx_device_str_prop_t{.key = lib::BOBOLINK.c_str(), .property_value = str_prop_int_val(10)},
      zx_device_str_prop_t{.key = lib::FLAG.c_str(),
                           .property_value = str_prop_bool_val(lib::FLAG_ENABLE)},
      zx_device_str_prop_t{.key = parent::PIZZA.c_str(),
                           .property_value = str_prop_str_val(parent::PIZZA_PEPPERONI.c_str())},
      zx_device_str_prop_t{.key = parent::GRIT.c_str(),
                           .property_value = str_prop_int_val(parent::GRIT_COARSE)},
  };

  args.str_props = str_props;
  args.str_prop_count = std::size(str_props);

  return device_add(parent, &args, &dev);
}

static constexpr zx_driver_ops_t driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = bind_func;
  return ops;
}();

ZIRCON_DRIVER(bindlib_codegen_test_parent, driver_ops, "zircon", "0.1");
