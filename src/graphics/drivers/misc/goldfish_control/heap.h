// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_CONTROL_HEAP_H_
#define SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_CONTROL_HEAP_H_

#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fit/function.h>

#include <memory>
#include <string>

#include <ddk/debug.h>
#include <fbl/intrusive_double_list.h>

namespace goldfish {

class Control;
using HeapInterface = ::llcpp::fuchsia::sysmem2::Heap::Interface;

// LLCPP synchronous server of a goldfish device-local Fuchsia sysmem Heap
// interface.
//
// Each Heap service runs on its own thread and has its own async loop.
class Heap : public HeapInterface, public fbl::DoublyLinkedListable<std::unique_ptr<Heap>> {
 public:
  // |llcpp::fuchsia::sysmem2::Heap::Interface|
  ~Heap() override;

  // |llcpp::fuchsia::sysmem2::Heap::Interface|
  void AllocateVmo(uint64_t size, AllocateVmoCompleter::Sync& completer) override = 0;

  // |llcpp::fuchsia::sysmem2::Heap::Interface|
  void CreateResource(::zx::vmo vmo, llcpp::fuchsia::sysmem2::SingleBufferSettings buffer_settings,
                      CreateResourceCompleter::Sync& completer) override = 0;

  // |llcpp::fuchsia::sysmem2::Heap::Interface|
  void DestroyResource(uint64_t id, DestroyResourceCompleter::Sync& completer) override = 0;

  // Bind the server to a FIDL channel.
  // The server should not be bound to any channel when Bind() is called.
  virtual void Bind(zx::channel server_request) = 0;

 protected:
  // This constructor is used only by its subclasses. To create a Heap
  // instance, use |Create()| static method of each subclass instead.
  explicit Heap(Control* control, const char* tag);

  // This helper method is used only by subclasses to bind to sysmem using
  // given channel and send |heap_properties| to sysmem.
  void BindWithHeapProperties(zx::channel server_request,
                              llcpp::fuchsia::sysmem2::HeapProperties heap_properties);

  Control* control() const { return control_; }

  async::Loop* loop() { return &loop_; }

 private:
  void OnClose(fidl::UnbindInfo info, zx::channel channel);

  Control* control_;

  async::Loop loop_;

  std::string tag_;
};

}  // namespace goldfish

#endif  // SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_CONTROL_HEAP_H_
