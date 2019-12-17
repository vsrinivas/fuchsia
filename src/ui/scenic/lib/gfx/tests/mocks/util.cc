// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/tests/mocks/util.h"

#include <lib/zx/vmo.h>

#include "gtest/gtest.h"
#include "src/lib/fsl/handles/object_info.h"

namespace scenic_impl::gfx::test {

bool IsEventSignalled(const zx::event& fence, zx_signals_t signal) {
  zx_signals_t pending = 0u;
  fence.wait_one(signal, zx::time(), &pending);
  return (pending & signal) != 0u;
}

zx::event CopyEvent(const zx::event& event) {
  zx::event event_copy;
  if (event.duplicate(ZX_RIGHT_SAME_RIGHTS, &event_copy) != ZX_OK)
    FXL_LOG(ERROR) << "Copying zx::event failed.";
  return event_copy;
}

std::vector<zx::event> CopyEventIntoFidlArray(const zx::event& event) {
  std::vector<zx::event> event_array;
  event_array.push_back(CopyEvent(event));
  return event_array;
}

zx::eventpair CopyEventPair(const zx::eventpair& eventpair) {
  zx::eventpair eventpair_copy;
  if (eventpair.duplicate(ZX_RIGHT_SAME_RIGHTS, &eventpair_copy) != ZX_OK)
    FXL_LOG(ERROR) << "Copying zx::eventpair failed.";
  return eventpair_copy;
}

uint64_t GetVmoSize(const zx::vmo& vmo) {
  uint64_t size;
  if (vmo.get_size(&size) != ZX_OK) {
    FXL_LOG(ERROR) << "Getting zx::vmo size failed";
    return 0u;
  }
  return size;
}

zx::vmo CopyVmo(const zx::vmo& vmo) {
  zx::vmo vmo_copy;
  if (vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy) != ZX_OK)
    FXL_LOG(ERROR) << "Copying zx::vmo failed.";
  return vmo_copy;
}

zx::event CreateEvent() {
  zx::event event;
  FXL_CHECK(zx::event::create(0, &event) == ZX_OK);
  return event;
}

std::vector<zx::event> CreateEventArray(size_t n) {
  std::vector<zx::event> events;
  for (size_t i = 0; i < n; i++) {
    events.push_back(CreateEvent());
  }
  return events;
}

fxl::RefPtr<fsl::SharedVmo> CreateSharedVmo(size_t size) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, 0u, &vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create vmo: status=" << status << ", size=" << size;
    return nullptr;
  }

  // Optimization: We will be writing to every page of the buffer, so
  // allocate physical memory for it eagerly.
  status = vmo.op_range(ZX_VMO_OP_COMMIT, 0u, size, nullptr, 0u);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to commit all pages of vmo: status=" << status << ", size=" << size;
    return nullptr;
  }

  uint32_t map_flags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  return fxl::MakeRefCounted<fsl::SharedVmo>(std::move(vmo), map_flags);
}

SessionWrapper::SessionWrapper(scenic_impl::Scenic* scenic) {
  FXL_CHECK(scenic);

  fuchsia::ui::scenic::SessionPtr session_ptr;

  fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener_handle;
  fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener> listener_request =
      listener_handle.NewRequest();

  scenic->CreateSession(session_ptr.NewRequest(), std::move(listener_handle));
  session_ = std::make_unique<scenic::Session>(std::move(session_ptr), std::move(listener_request));
  session_anchor_ = std::make_unique<scenic::EntityNode>(session_.get());

  session_->set_event_handler([this](std::vector<fuchsia::ui::scenic::Event> events) {
    for (fuchsia::ui::scenic::Event& event : events) {
      events_.push_back(std::move(event));
    }
  });
}

SessionWrapper::SessionWrapper(
    scenic_impl::Scenic* scenic,
    fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser_request) {
  FXL_CHECK(scenic);

  fuchsia::ui::scenic::SessionPtr session_ptr;

  fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener_handle;
  fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener> listener_request =
      listener_handle.NewRequest();

  scenic->CreateSession2(session_ptr.NewRequest(), std::move(listener_handle),
                         std::move(view_focuser_request));
  session_ = std::make_unique<scenic::Session>(std::move(session_ptr), std::move(listener_request));
  session_anchor_ = std::make_unique<scenic::EntityNode>(session_.get());

  session_->set_event_handler([this](std::vector<fuchsia::ui::scenic::Event> events) {
    for (fuchsia::ui::scenic::Event& event : events) {
      events_.push_back(std::move(event));
    }
  });
}

SessionWrapper::~SessionWrapper() {
  session_anchor_.reset();  // Let go of the resource; enqueue the release cmd.
  session_->Flush();        // Ensure Scenic receives the release cmd.
}

void SessionWrapper::RunNow(
    fit::function<void(scenic::Session* session, scenic::EntityNode* session_anchor)>
        create_scene_callback) {
  create_scene_callback(session_.get(), session_anchor_.get());
}

}  // namespace scenic_impl::gfx::test
