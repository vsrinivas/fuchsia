// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header has to come first, and we define our ZX_PROTOCOL, so that
// we don't have to edit protodefs.h to add this test protocol.
#include <bind/fuchsia/compat/cpp/fidl.h>
#define ZX_PROTOCOL_PARENT bind::fuchsia::compat::BIND_PROTOCOL_PARENT

#include <fuchsia/hardware/compat/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/intrusive_double_list.h>

#include "src/devices/tests/compat/root/root-bind.h"

namespace root {

class Root;
using DeviceType = ddk::Device<Root, ddk::Initializable>;
class Root : public DeviceType, public ddk::ParentProtocol<Root, ddk::base_protocol> {
 public:
  explicit Root(zx_device_t* root) : DeviceType(root) {}
  virtual ~Root() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev) {
    auto driver = std::make_unique<Root>(dev);
    zx_status_t status = driver->Bind();
    if (status != ZX_OK) {
      return status;
    }
    // The DriverFramework now owns driver.
    __UNUSED auto ptr = driver.release();
    return ZX_OK;
  }

  zx_status_t Bind() { return DdkAdd(ddk::DeviceAddArgs("root")); }

  void DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

  void DdkRelease() { delete this; }

  void ParentGetString(char* out_response, size_t response_capacity) {
    strncpy(out_response, "hello world!", response_capacity);
  }
};

static zx_driver_ops_t root_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Root::Bind;
  return ops;
}();

}  // namespace root

ZIRCON_DRIVER(Root, root::root_driver_ops, "zircon", "0.1");
