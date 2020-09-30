// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_CONTROL_DEVICE_LOCAL_HEAP_H_
#define SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_CONTROL_DEVICE_LOCAL_HEAP_H_

#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fit/function.h>

#include <memory>

#include <ddk/debug.h>
#include <fbl/intrusive_double_list.h>

#include "src/graphics/drivers/misc/goldfish_control/heap.h"

namespace goldfish {

class Control;
using HeapInterface = ::llcpp::fuchsia::sysmem2::Heap::Interface;

// LLCPP synchronous server of a goldfish device-local Fuchsia sysmem Heap
// interface.
class DeviceLocalHeap : public Heap {
 public:
  static std::unique_ptr<DeviceLocalHeap> Create(Control* control);

  ~DeviceLocalHeap() override;

  // |llcpp::fuchsia::sysmem2::Heap::Interface|
  void AllocateVmo(uint64_t size, AllocateVmoCompleter::Sync completer) override;

  // |llcpp::fuchsia::sysmem2::Heap::Interface|
  void CreateResource(::zx::vmo vmo, llcpp::fuchsia::sysmem2::SingleBufferSettings buffer_settings,
                      CreateResourceCompleter::Sync completer) override;

  // |llcpp::fuchsia::sysmem2::Heap::Interface|
  void DestroyResource(uint64_t id, DestroyResourceCompleter::Sync completer) override;

  // |Heap|
  void Bind(zx::channel server_request) override;

 private:
  // This constructor is for internal use only. Use |DeviceLocalHeap::Create()| instead.
  explicit DeviceLocalHeap(Control* control);
};

}  // namespace goldfish

#endif  // SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_CONTROL_DEVICE_LOCAL_HEAP_H_
