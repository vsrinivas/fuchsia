// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/composite-driver-v1/test-root/test_root.h"

#include <lib/async/cpp/task.h>
#include <lib/sync/cpp/completion.h>

#include "src/devices/tests/composite-driver-v1/test-root/test_root-bind.h"

namespace test_root {

zx_status_t TestRoot::Bind(void* ctx, zx_device_t* dev) {
  {
    zx_device_prop_t props[] = {
        {BIND_PCI_VID, 0, 1},
        {BIND_PCI_DID, 0, 0},
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
        {BIND_PCI_DID, 0, 0},
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
        {BIND_PCI_DID, 0, 0},
    };
    auto device = std::make_unique<TestRoot>(dev);
    zx_status_t status = device->Bind("child_c", props);
    if (status != ZX_OK) {
      return status;
    }
    __UNUSED auto ptr = device.release();
  }

  {
    zx_device_prop_t props[] = {
        {BIND_PCI_VID, 0, 1},
        {BIND_PCI_DID, 0, 1},
    };
    auto device = std::make_unique<TestRoot>(dev);
    zx_status_t status = device->Bind("fragment_a", props);
    if (status != ZX_OK) {
      return status;
    }
    __UNUSED auto ptr = device.release();
  }

  {
    zx_device_prop_t props[] = {
        {BIND_PCI_VID, 0, 2},
        {BIND_PCI_DID, 0, 1},
    };
    auto device = std::make_unique<TestRoot>(dev);
    zx_status_t status = device->Bind("fragment_b", props);
    if (status != ZX_OK) {
      return status;
    }
    __UNUSED auto ptr = device.release();
  }

  {
    zx_device_prop_t props[] = {
        {BIND_PCI_VID, 0, 3},
        {BIND_PCI_DID, 0, 1},
    };
    auto device = std::make_unique<TestRoot>(dev);
    zx_status_t status = device->Bind("fragment_c", props);
    if (status != ZX_OK) {
      return status;
    }
    const zx_bind_inst_t fragment1_match[] = {
        BI_ABORT_IF(NE, BIND_PCI_VID, 1),
        BI_MATCH_IF(EQ, BIND_PCI_DID, 1),
    };
    const zx_bind_inst_t fragment2_match[] = {
        BI_ABORT_IF(NE, BIND_PCI_VID, 2),
        BI_MATCH_IF(EQ, BIND_PCI_DID, 1),
    };
    const zx_bind_inst_t fragment3_match[] = {
        BI_ABORT_IF(NE, BIND_PCI_VID, 3),
        BI_MATCH_IF(EQ, BIND_PCI_DID, 1),
    };
    const device_fragment_part_t fragment1[] = {
        {std::size(fragment1_match), fragment1_match},
    };
    const device_fragment_part_t fragment2[] = {
        {std::size(fragment2_match), fragment2_match},
    };
    const device_fragment_part_t fragment3[] = {
        {std::size(fragment3_match), fragment3_match},
    };
    const device_fragment_t fragments[] = {
        {"a", std::size(fragment1), fragment1},
        {"b", std::size(fragment2), fragment2},
        {"c", std::size(fragment3), fragment3},
    };

    const zx_device_prop_t new_props[] = {
        {BIND_PCI_VID, 0, 4},
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

    status = device_add_composite(device->zxdev(), "composite-device", &comp_desc);
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
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  libsync::Completion init_complete;
  zx_status_t init_status;
  async::TaskClosure init_task([&] {
    outgoing_ = component::OutgoingDirectory::Create(loop_.dispatcher());
    auto serve_status = outgoing_->AddProtocol<fuchsia_composite_test::Device>(&this->server_);
    if (serve_status.status_value() != ZX_OK) {
      init_status = serve_status.status_value();
      return;
    }

    serve_status = outgoing_->Serve(std::move(endpoints->server));
    if (serve_status.status_value() != ZX_OK) {
      init_status = serve_status.status_value();
      return;
    }

    init_status = ZX_OK;
    init_complete.Signal();
  });
  if (zx_status_t status = init_task.Post(loop_.dispatcher()); status != ZX_OK) {
    return status;
  }
  init_complete.Wait();
  if (init_status != ZX_OK) {
    return init_status;
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

TestRoot::~TestRoot() {
  if (!outgoing_) {
    return;
  }
  libsync::Completion shutdown_complete;
  async::TaskClosure shutdown_task([&] {
    outgoing_.reset();
    shutdown_complete.Signal();
  });
  if (zx_status_t status = shutdown_task.Post(loop_.dispatcher()); status != ZX_OK) {
    return;
  }
  shutdown_complete.Wait();
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
