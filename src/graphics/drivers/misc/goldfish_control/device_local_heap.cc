// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/drivers/misc/goldfish_control/device_local_heap.h"

#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fit/function.h>

#include <memory>

#include <ddk/debug.h>
#include <fbl/intrusive_double_list.h>

#include "src/graphics/drivers/misc/goldfish_control/control_device.h"

namespace goldfish {

namespace {

static const char* kTag = "goldfish-device-local-heap";

llcpp::fuchsia::sysmem2::HeapProperties GetHeapProperties() {
  auto coherency_domain_support =
      std::make_unique<llcpp::fuchsia::sysmem2::CoherencyDomainSupport>();
  *coherency_domain_support =
      llcpp::fuchsia::sysmem2::CoherencyDomainSupport::Builder(
          std::make_unique<llcpp::fuchsia::sysmem2::CoherencyDomainSupport::Frame>())
          .set_cpu_supported(std::make_unique<bool>(false))
          .set_ram_supported(std::make_unique<bool>(false))
          .set_inaccessible_supported(std::make_unique<bool>(true))
          .build();

  return llcpp::fuchsia::sysmem2::HeapProperties::Builder(
             std::make_unique<llcpp::fuchsia::sysmem2::HeapProperties::Frame>())
      .set_coherency_domain_support(std::move(coherency_domain_support))
      .set_need_clear(std::make_unique<bool>(false))
      .build();
}

}  // namespace

// static
std::unique_ptr<DeviceLocalHeap> DeviceLocalHeap::Create(Control* control) {
  // Using `new` to access a non-public constructor.
  return std::unique_ptr<DeviceLocalHeap>(new DeviceLocalHeap(control));
}

DeviceLocalHeap::DeviceLocalHeap(Control* control) : Heap(control, kTag) {}

DeviceLocalHeap::~DeviceLocalHeap() = default;

void DeviceLocalHeap::AllocateVmo(uint64_t size, AllocateVmoCompleter::Sync completer) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, 0, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] zx::vmo::create() failed - size: %lu status: %d", kTag, size, status);
    completer.Reply(status, zx::vmo{});
  } else {
    completer.Reply(ZX_OK, std::move(vmo));
  }
}

void DeviceLocalHeap::CreateResource(::zx::vmo vmo,
                                     llcpp::fuchsia::sysmem2::SingleBufferSettings buffer_settings,
                                     CreateResourceCompleter::Sync completer) {
  uint64_t id = control()->RegisterBufferHandle(vmo);
  if (id == ZX_KOID_INVALID) {
    completer.Reply(ZX_ERR_INVALID_ARGS, 0u);
  } else {
    completer.Reply(ZX_OK, id);
  }
}

void DeviceLocalHeap::DestroyResource(uint64_t id, DestroyResourceCompleter::Sync completer) {
  control()->FreeBufferHandle(id);
  completer.Reply();
}

void DeviceLocalHeap::Bind(zx::channel server_request) {
  BindWithHeapProperties(std::move(server_request), GetHeapProperties());
}

}  // namespace goldfish
