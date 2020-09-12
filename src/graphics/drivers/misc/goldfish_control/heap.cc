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

Heap::Heap(Control* control, const char* tag)
    : control_(control), loop_(&kAsyncLoopConfigNoAttachToCurrentThread), tag_(tag) {
  ZX_DEBUG_ASSERT(control_);

  // Start server thread. Heap server must be running on a seperate
  // thread as sysmem might be making synchronous allocation requests
  // from the main thread.
  std::string thread_name = tag_ + "-thread";
  loop_.StartThread(thread_name.c_str());
}

Heap::~Heap() { loop_.Shutdown(); }

void Heap::BindWithHeapProperties(zx::channel server_request,
                                  llcpp::fuchsia::sysmem2::HeapProperties heap_properties) {
  zx_handle_t server_handle = server_request.release();
  async::PostTask(loop_.dispatcher(), [server_handle, heap_properties = std::move(heap_properties),
                                       this]() mutable {
    auto result = fidl::BindServer<HeapInterface>(
        loop_.dispatcher(), zx::channel(server_handle), static_cast<HeapInterface*>(this),
        [](HeapInterface* interface, fidl::UnbindInfo info, zx::channel channel) {
          static_cast<Heap*>(interface)->OnClose(info, std::move(channel));
        });
    if (!result.is_ok()) {
      zxlogf(ERROR, "[%s] Cannot bind to channel: status: %d", tag_.c_str(), result.error());
      control_->RemoveHeap(this);
      return;
    }
    result.value()->OnRegister(std::move(heap_properties));
  });
}

void Heap::OnClose(fidl::UnbindInfo info, zx::channel channel) {
  if (info.reason == fidl::UnbindInfo::kPeerClosed) {
    zxlogf(INFO, "[%s] Client closed Heap connection: epitaph: %d", tag_.c_str(), info.status);
  } else if (info.reason != fidl::UnbindInfo::kUnbind && info.reason != fidl::UnbindInfo::kClose) {
    zxlogf(ERROR, "[%s] Channel internal error: status: %d", tag_.c_str(), info.status);
  }

  control_->RemoveHeap(this);
}

}  // namespace goldfish
