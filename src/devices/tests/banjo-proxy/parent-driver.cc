// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>

#include <string>

#include <ddktl/device.h>
#include <ddktl/unbind-txn.h>

#include "src/devices/tests/banjo-proxy/parent-driver-bind.h"

namespace {

class Device;
using DeviceParent = ddk::Device<Device, ddk::Unbindable>;

class Device : public DeviceParent, public ddk::SysmemProtocol<Device, ddk::base_protocol> {
 public:
  static zx_status_t Bind(void* ctx, zx_device_t* parent) {
    auto device = std::make_unique<Device>(parent);

    // Add our child to match to the composite.
    zx_device_prop_t props[] = {{
        .id = BIND_PCI_VID,
        .value = 1,
    }};
    zx_status_t status = device->DdkAdd(ddk::DeviceAddArgs("parent").set_props(props));
    // Create a composite device.
    {
      const zx_bind_inst_t fragment1_match[] = {
          BI_ABORT_IF(NE, BIND_PCI_VID, 1),
          BI_MATCH_IF(EQ, BIND_PCI_VID, 1),
      };
      const device_fragment_part_t fragment1[] = {
          {std::size(fragment1_match), fragment1_match},
      };
      const zx_device_prop_t new_props[] = {
          {BIND_PCI_VID, 0, 4},
      };
      const device_fragment_t fragments[] = {
          {"a", std::size(fragment1), fragment1},
      };
      const composite_device_desc_t comp_desc = {
          .props = new_props,
          .props_count = std::size(new_props),
          .fragments = fragments,
          .fragments_count = std::size(fragments),
          .primary_fragment = "a",
          .spawn_colocated = false,
          .metadata_list = nullptr,
          .metadata_count = 0,
      };
      zx_status_t status = device_add_composite(device->zxdev(), "composite", &comp_desc);
      if (status != ZX_OK) {
        return status;
      }
    }

    if (status == ZX_OK) {
      __UNUSED auto ptr = device.release();
    } else {
      zxlogf(ERROR, "Failed to add device");
    }

    return status;
  }

  Device(zx_device_t* parent) : DeviceParent(parent) {}

  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  zx_status_t SysmemConnect(zx::channel allocator_request) { return ZX_ERR_STOP; }
  zx_status_t SysmemRegisterHeap(uint64_t heap, zx::channel heap_connection) { return ZX_ERR_STOP; }
  zx_status_t SysmemRegisterSecureMem(zx::channel secure_mem_connection) { return ZX_ERR_STOP; }
  zx_status_t SysmemUnregisterSecureMem() { return ZX_ERR_STOP; }

 private:
};

static constexpr zx_driver_ops_t kDriverOps = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Device::Bind;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(fidl_protocol_test_parent, kDriverOps, "zircon", "0.1");
