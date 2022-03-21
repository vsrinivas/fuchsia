// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>

#include <string>

#include <bind/bindlib/to/fidl/testlib/cpp/fidl.h>
#include <bind/bindlibparent/to/fidl/testlib/cpp/fidl.h>

#include "src/devices/tests/bindlib-to-fidl-codegen-test/parent-driver-bind.h"

namespace bindlib = bind::bindlib::to::fidl::testlib;
namespace bindlibparent = bind::bindlibparent::to::fidl::testlib;

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
      {BIND_PCI_VID, 0, bindlib::BIND_PCI_VID_PIE},
      {BIND_PCI_DID, 0, 1234},
  };
  args.props = props;
  args.prop_count = std::size(props);

  zx_device_str_prop_t str_props[] = {
      zx_device_str_prop_t{.key = bindlib::KINGLET,
                           .property_value = str_prop_str_val("firecrest")},
      zx_device_str_prop_t{.key = bindlib::MOON,
                           .property_value = str_prop_enum_val(bindlib::MOON_HALF)},
      zx_device_str_prop_t{.key = bindlib::BOBOLINK, .property_value = str_prop_int_val(10)},
      zx_device_str_prop_t{.key = bindlib::FLAG,
                           .property_value = str_prop_bool_val(bindlib::FLAG_ENABLE)},
      zx_device_str_prop_t{.key = bindlibparent::PIZZA,
                           .property_value = str_prop_str_val(bindlibparent::PIZZA_PEPPERONI)},
      zx_device_str_prop_t{.key = bindlibparent::GRIT,
                           .property_value = str_prop_int_val(bindlibparent::GRIT_COARSE)},
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

ZIRCON_DRIVER(bindlib_fidl_test_parent, driver_ops, "zircon", "0.1");
