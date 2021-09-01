// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/drivers/misc/goldfish_control/device_local_heap.h"

#include <fidl/fuchsia.sysmem2/cpp/wire.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/ddk/debug.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fit/function.h>

#include <memory>

#include <fbl/intrusive_double_list.h>

#include "src/graphics/drivers/misc/goldfish_control/control_device.h"

namespace goldfish {

namespace {

static const char* kTag = "goldfish-device-local-heap";

fuchsia_sysmem2::wire::HeapProperties GetHeapProperties(fidl::AnyArena& allocator) {
  fuchsia_sysmem2::wire::CoherencyDomainSupport coherency_domain_support(allocator);
  coherency_domain_support.set_cpu_supported(allocator, false)
      .set_ram_supported(allocator, false)
      .set_inaccessible_supported(allocator, true);

  fuchsia_sysmem2::wire::HeapProperties heap_properties(allocator);
  heap_properties.set_coherency_domain_support(allocator, std::move(coherency_domain_support))
      .set_need_clear(allocator, false);
  return heap_properties;
}

}  // namespace

// static
std::unique_ptr<DeviceLocalHeap> DeviceLocalHeap::Create(Control* control) {
  // Using `new` to access a non-public constructor.
  return std::unique_ptr<DeviceLocalHeap>(new DeviceLocalHeap(control));
}

DeviceLocalHeap::DeviceLocalHeap(Control* control) : Heap(control, kTag) {}

DeviceLocalHeap::~DeviceLocalHeap() = default;

void DeviceLocalHeap::AllocateVmo(AllocateVmoRequestView request,
                                  AllocateVmoCompleter::Sync& completer) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(request->size, 0, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] zx::vmo::create() failed - size: %lu status: %d", kTag, request->size,
           status);
    completer.Reply(status, zx::vmo{});
  } else {
    completer.Reply(ZX_OK, std::move(vmo));
  }
}

void DeviceLocalHeap::CreateResource(CreateResourceRequestView request,
                                     CreateResourceCompleter::Sync& completer) {
  uint64_t id = control()->RegisterBufferHandle(request->vmo);
  if (id == ZX_KOID_INVALID) {
    completer.Reply(ZX_ERR_INVALID_ARGS, 0u);
  } else {
    completer.Reply(ZX_OK, id);
  }
}

void DeviceLocalHeap::DestroyResource(DestroyResourceRequestView request,
                                      DestroyResourceCompleter::Sync& completer) {
  control()->FreeBufferHandle(request->id);
  completer.Reply();
}

void DeviceLocalHeap::Bind(zx::channel server_request) {
  auto allocator = std::make_unique<fidl::Arena<512>>();
  fuchsia_sysmem2::wire::HeapProperties heap_properties = GetHeapProperties(*allocator.get());
  BindWithHeapProperties(std::move(server_request), std::move(allocator),
                         std::move(heap_properties));
}

}  // namespace goldfish
