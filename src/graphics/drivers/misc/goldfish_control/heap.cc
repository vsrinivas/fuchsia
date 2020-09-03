// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/drivers/misc/goldfish_control/heap.h"

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

static const char* kTag = "device-local-heap";

static const char* kThreadName = "goldfish_device_local_heap_thread";

llcpp::fuchsia::sysmem2::HeapProperties GetDeviceLocalHeapProperties() {
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
      .build();
}

}  // namespace

// static
std::unique_ptr<Heap> Heap::Create(Control* control) {
  // Using `new` to access a non-public constructor.
  return std::unique_ptr<Heap>(new Heap(control));
}

Heap::Heap(Control* control) : control_(control), loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
  ZX_DEBUG_ASSERT(control_);

  // Start server thread. Heap server must be running on a seperate
  // thread as sysmem might be making synchronous allocation requests
  // from the main thread.
  loop_.StartThread(kThreadName);
}

Heap::~Heap() { loop_.Shutdown(); }

void Heap::AllocateVmo(uint64_t size, AllocateVmoCompleter::Sync completer) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, 0, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] zx::vmo::create() failed - size: %lu status: %d", kTag, size, status);
    completer.Reply(status, zx::vmo{});
  } else {
    completer.Reply(ZX_OK, std::move(vmo));
  }
}

void Heap::CreateResource(::zx::vmo vmo, CreateResourceCompleter::Sync completer) {
  uint64_t id = control_->RegisterBufferHandle(vmo);
  if (id == ZX_KOID_INVALID) {
    completer.Reply(ZX_ERR_INVALID_ARGS, 0u);
  } else {
    completer.Reply(ZX_OK, id);
  }
}

void Heap::DestroyResource(uint64_t id, DestroyResourceCompleter::Sync completer) {
  control_->FreeBufferHandle(id);
  completer.Reply();
}

void Heap::Bind(zx::channel server_request) {
  zx_handle_t server_handle = server_request.release();
  async::PostTask(loop_.dispatcher(), [server_handle, this] {
    auto result = fidl::BindServer<HeapInterface>(
        loop_.dispatcher(), zx::channel(server_handle), static_cast<HeapInterface*>(this),
        [](HeapInterface* interface, fidl::UnbindInfo info, zx::channel channel) {
          static_cast<Heap*>(interface)->OnClose(info, std::move(channel));
        });
    if (!result.is_ok()) {
      zxlogf(ERROR, "[%s] Cannot bind to channel: status: %d", kTag, result.error());
      control_->RemoveHeap(this);
      return;
    }
    result.value()->OnRegister(GetDeviceLocalHeapProperties());
  });
}

void Heap::OnClose(fidl::UnbindInfo info, zx::channel channel) {
  if (info.reason == fidl::UnbindInfo::kPeerClosed) {
    zxlogf(INFO, "[%s] Client closed Heap connection: epitaph: %d", kTag, info.status);
  } else if (info.reason != fidl::UnbindInfo::kUnbind && info.reason != fidl::UnbindInfo::kClose) {
    zxlogf(ERROR, "[%s] Channel internal error: status: %d", kTag, info.status);
  }

  control_->RemoveHeap(this);
}

}  // namespace goldfish
