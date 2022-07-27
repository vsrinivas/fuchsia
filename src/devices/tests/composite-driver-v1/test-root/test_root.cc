// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/composite-driver-v1/test-root/test_root.h"

#include "src/devices/tests/composite-driver-v1/test-root/test_root-bind.h"

namespace test_root {

zx_status_t TestRoot::Bind(void* ctx, zx_device_t* dev) {
  {
    zx_device_prop_t props[] = {
        {BIND_PCI_VID, 0, 1},
    };
    auto device = std::make_unique<TestRoot>(dev);
    zx_status_t status = device->Bind("child_a", props);
    if (status != ZX_OK) {
      return status;
    }
    __UNUSED auto ptr = device.release();
  }

  {
    zx_device_prop_t props[] = {
        {BIND_PCI_VID, 0, 2},
    };
    auto device = std::make_unique<TestRoot>(dev);
    zx_status_t status = device->Bind("child_b", props);
    if (status != ZX_OK) {
      return status;
    }
    __UNUSED auto ptr = device.release();
  }

  {
    zx_device_prop_t props[] = {
        {BIND_PCI_VID, 0, 3},
    };
    auto device = std::make_unique<TestRoot>(dev);
    zx_status_t status = device->Bind("child_c", props);
    if (status != ZX_OK) {
      return status;
    }
    __UNUSED auto ptr = device.release();
  }

  return ZX_OK;
}

zx_status_t TestRoot::Bind(const char* name, cpp20::span<const zx_device_prop_t> props) {
  server_ = NumberServer(props[0].value);
  if (auto status = loop_.StartThread("test-root-dispatcher-thread"); status != ZX_OK) {
    return status;
  }
  outgoing_ = component::OutgoingDirectory::Create(loop_.dispatcher());
  auto serve_status = outgoing_->AddProtocol<fuchsia_composite_test::Device>(&this->server_);
  if (serve_status.status_value() != ZX_OK) {
    return serve_status.status_value();
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }
  serve_status = outgoing_->Serve(std::move(endpoints->server));
  if (serve_status.status_value() != ZX_OK) {
    return serve_status.status_value();
  }
  std::array<const char*, 1> offers = {"fuchsia.composite.test.Device"};

  is_bound.Set(true);

  return DdkAdd(ddk::DeviceAddArgs(name)
                    .set_props(props)
                    .set_inspect_vmo(inspect_.DuplicateVmo())
                    .set_fidl_protocol_offers(offers)
                    .set_flags(DEVICE_ADD_MUST_ISOLATE)
                    .set_outgoing_dir(endpoints->client.TakeChannel()));
}

void TestRoot::DdkInit(ddk::InitTxn txn) {
  uint32_t data = server_.number() + 3;
  DdkAddMetadata(server_.number(), &data, sizeof(data));

  txn.Reply(ZX_OK);
}

void TestRoot::DdkRelease() { delete this; }

static zx_driver_ops_t test_root_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = TestRoot::Bind;
  return ops;
}();

}  // namespace test_root

ZIRCON_DRIVER(TestRoot, test_root::test_root_driver_ops, "zircon", "0.1");
