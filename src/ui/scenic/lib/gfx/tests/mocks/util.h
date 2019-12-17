// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_TESTS_UTIL_H_
#define SRC_UI_SCENIC_LIB_GFX_TESTS_UTIL_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/zx/event.h>
#include <lib/zx/eventpair.h>

#include <optional>

#include "lib/fidl/cpp/vector.h"
#include "src/lib/fsl/vmo/shared_vmo.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/time/time_delta.h"
#include "src/ui/scenic/lib/scenic/scenic.h"

namespace scenic_impl::gfx::test {

// Synchronously checks whether the event has signalled any of the bits in
// |signal|.
bool IsEventSignalled(const zx::event& event, zx_signals_t signal);

// Create a duplicate of the event.
zx::event CopyEvent(const zx::event& event);

// Create a duplicate of the event, and create a new fidl Array of size one to
// wrap it.
std::vector<zx::event> CopyEventIntoFidlArray(const zx::event& event);

// Create a duplicate of the eventpair.
zx::eventpair CopyEventPair(const zx::eventpair& eventpair);

// Get the size of the VMO.
uint64_t GetVmoSize(const zx::vmo& vmo);

// Create a duplicate of the VMO.
zx::vmo CopyVmo(const zx::vmo& vmo);

// Create an event.
zx::event CreateEvent();

// Create a std::vector and populate with |n| newly created events.
std::vector<zx::event> CreateEventArray(size_t n);

// Creates a VMO with the specified size, immediately allocate physical memory
// for it, and wraps in a |fsl::SharedVmo| to make it easy to map it into the
// caller's address space.
fxl::RefPtr<fsl::SharedVmo> CreateSharedVmo(size_t size);

// A little wrapper class to capture state for managing a GFX session.
// Tests may freely subclass this type to add more state for their specific purposes.
class SessionWrapper {
 public:
  explicit SessionWrapper(scenic_impl::Scenic* scenic);
  explicit SessionWrapper(scenic_impl::Scenic* scenic,
                          fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser_request);
  virtual ~SessionWrapper();

  scenic::Session* session() { return session_.get(); }

  // Allow caller to run some code in the context of this particular session.
  void RunNow(fit::function<void(scenic::Session* session, scenic::EntityNode* session_anchor)>
                  create_scene_callback);

 private:
  // Client-side session object.
  std::unique_ptr<scenic::Session> session_;

  // Clients attach their nodes here to participate in the global scene graph.
  std::unique_ptr<scenic::EntityNode> session_anchor_;

  // Collect all events received.
  std::vector<fuchsia::ui::scenic::Event> events_;
};

}  // namespace scenic_impl::gfx::test

#endif  // SRC_UI_SCENIC_LIB_GFX_TESTS_UTIL_H_
