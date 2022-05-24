// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/composite-driver-v1/composite_driver_v1.h"

#include <fidl/fuchsia.composite.test/cpp/wire.h>

#include <set>

#include "src/devices/tests/composite-driver-v1/composite_driver_v1-bind.h"

namespace composite_driver_v1 {

zx::status<uint32_t> DoFidlConnections(zx_device_t* dev, const char* fragment) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_composite_test::Device>();
  zx_status_t status = device_connect_fragment_fidl_protocol(
      dev, fragment, "fuchsia.composite.test.Device", endpoints->server.TakeChannel().release());
  if (status != ZX_OK) {
    zxlogf(INFO, "Failed to connect: %s", zx_status_get_string(status));
    return zx::error(status);
  }
  auto result = fidl::WireCall(endpoints->client)->GetNumber();
  if (result.status() != ZX_OK) {
    zxlogf(ERROR, "Failed to call number: %s", result.lossy_description());
    return zx::error(result.status());
  }
  return zx::ok(result.value_NEW().number);
}

zx_status_t CompositeDriverV1::Bind(void* ctx, zx_device_t* dev) {
  uint32_t count = device_get_fragment_count(dev);
  if (count != 3) {
    zxlogf(ERROR, "Wrong fragment count: expected 3, got %d", count);
    return ZX_ERR_INTERNAL;
  }

  std::vector<composite_device_fragment_t> fragments(count);
  std::set<std::string> expected_fragments;
  expected_fragments.insert("a");
  expected_fragments.insert("b");
  expected_fragments.insert("c");

  size_t actual = 0;
  bool error = false;
  device_get_fragments(dev, fragments.data(), fragments.size(), &actual);
  for (auto& fragment : fragments) {
    if (expected_fragments.count(fragment.name) == 1) {
      expected_fragments.erase(fragment.name);
    } else {
      zxlogf(ERROR, "Found unexpected fragment: %s", fragment.name);
      error = true;
    }
  }
  if (!expected_fragments.empty()) {
    error = true;
    for (auto& fragment : expected_fragments) {
      zxlogf(ERROR, "Didn't find expected fragment: %s", fragment.data());
    }
  }
  if (error) {
    return ZX_ERR_INTERNAL;
  }

  auto result = DoFidlConnections(dev, "a");
  if (result.status_value() != ZX_OK) {
    return result.status_value();
  }
  if (*result != 1) {
    zxlogf(ERROR, "Result for a is not correct: expected 1: got %d", *result);
    return ZX_ERR_INTERNAL;
  }
  result = DoFidlConnections(dev, "b");
  if (result.status_value() != ZX_OK) {
    return result.status_value();
  }
  if (*result != 2) {
    zxlogf(ERROR, "Result for b is not correct: expected 2: got %d", *result);
    return ZX_ERR_INTERNAL;
  }

  uint32_t metadata = 0;
  zx_status_t status = device_get_metadata(dev, 1, &metadata, sizeof(metadata), &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to get metadata 1: %s", zx_status_get_string(status));
    return ZX_ERR_INTERNAL;
  }
  if (metadata != 4) {
    zxlogf(ERROR, "Got wrong metadata: expected 4: got %d", metadata);
    return ZX_ERR_INTERNAL;
  }

  status = device_get_metadata(dev, 2, &metadata, sizeof(metadata), &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to get metadata 2: %s", zx_status_get_string(status));
    return ZX_ERR_INTERNAL;
  }
  if (metadata != 5) {
    zxlogf(ERROR, "Got wrong metadata: expected 5: got %d", metadata);
    return ZX_ERR_INTERNAL;
  }

  auto device = std::make_unique<CompositeDriverV1>(dev);
  status = device->Bind();
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto ptr = device.release();
  return ZX_OK;
}

zx_status_t CompositeDriverV1::Bind() {
  is_bound.Set(true);
  return DdkAdd(ddk::DeviceAddArgs("composite_child").set_inspect_vmo(inspect_.DuplicateVmo()));
}

void CompositeDriverV1::DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

void CompositeDriverV1::DdkRelease() { delete this; }

static zx_driver_ops_t composite_driver_v1_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = CompositeDriverV1::Bind;
  return ops;
}();

}  // namespace composite_driver_v1

ZIRCON_DRIVER(CompositeDriverV1, composite_driver_v1::composite_driver_v1_driver_ops, "zircon",
              "0.1");
