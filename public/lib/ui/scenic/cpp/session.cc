// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/scenic/cpp/session.h"

#include <stdio.h>
#include <zircon/assert.h>

#include "lib/ui/scenic/cpp/commands.h"

namespace scenic {

constexpr size_t kCommandsPerMessage =
    (ZX_CHANNEL_MAX_MSG_BYTES - sizeof(fidl_message_header_t) -
     sizeof(fidl_vector_t)) /
    sizeof(fuchsia::ui::scenic::Command);

SessionPtrAndListenerRequest CreateScenicSessionPtrAndListenerRequest(
    fuchsia::ui::scenic::Scenic* scenic) {
  fuchsia::ui::scenic::SessionPtr session;
  fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener_handle;
  auto listener_request = listener_handle.NewRequest();

  scenic->CreateSession(session.NewRequest(), listener_handle.Bind());

  return {std::move(session), std::move(listener_request)};
}

Session::Session(fuchsia::ui::scenic::SessionPtr session,
                 fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener>
                     session_listener)
    : session_(std::move(session)), session_listener_binding_(this) {
  ZX_DEBUG_ASSERT(session_);
  if (session_listener.is_valid())
    session_listener_binding_.Bind(std::move(session_listener));
}

Session::Session(fuchsia::ui::scenic::Scenic* scenic)
    : session_listener_binding_(this) {
  ZX_DEBUG_ASSERT(scenic);
  scenic->CreateSession(session_.NewRequest(),
                        session_listener_binding_.NewBinding());
}

Session::Session(SessionPtrAndListenerRequest session_and_listener)
    : Session(std::move(session_and_listener.first),
              std::move(session_and_listener.second)) {}

Session::~Session() {
  ZX_DEBUG_ASSERT_MSG(resource_count_ == 0,
                      "Some resources outlived the session: %u",
                      resource_count_);
}

uint32_t Session::AllocResourceId() {
  uint32_t resource_id = next_resource_id_++;
  ZX_DEBUG_ASSERT(resource_id);
  resource_count_++;
  return resource_id;
}

void Session::ReleaseResource(uint32_t resource_id) {
  resource_count_--;
  Enqueue(NewReleaseResourceCmd(resource_id));
}

void Session::Enqueue(fuchsia::ui::gfx::Command command) {
  Enqueue(NewCommand(std::move(command)));
}

void Session::Enqueue(fuchsia::ui::input::Command command) {
  Enqueue(NewCommand(std::move(command)));
}

void Session::Enqueue(fuchsia::ui::scenic::Command command) {
  commands_.push_back(std::move(command));
  if (commands_->size() >= kCommandsPerMessage ||
      command.Which() == fuchsia::ui::scenic::Command::Tag::kInput) {
    Flush();
  }
}

void Session::EnqueueAcquireFence(zx::event fence) {
  ZX_DEBUG_ASSERT(fence);
  acquire_fences_.push_back(std::move(fence));
}

void Session::EnqueueReleaseFence(zx::event fence) {
  ZX_DEBUG_ASSERT(fence);
  release_fences_.push_back(std::move(fence));
}

void Session::Flush() {
  ZX_DEBUG_ASSERT(session_);
  if (!commands_->empty()) {
    ZX_DEBUG_ASSERT(static_cast<bool>(commands_));
    session_->Enqueue(std::move(commands_));

    // After being moved, |commands_| is in a "valid but unspecified state";
    // see http://en.cppreference.com/w/cpp/utility/move.  Calling reset() makes
    // it safe to continue using.
    commands_.reset();
  }
}

void Session::Present(uint64_t presentation_time, PresentCallback callback) {
  ZX_DEBUG_ASSERT(session_);
  Flush();

  if (acquire_fences_.is_null())
    acquire_fences_.resize(0u);
  if (release_fences_.is_null())
    release_fences_.resize(0u);
  session_->Present(presentation_time, std::move(acquire_fences_),
                    std::move(release_fences_), std::move(callback));
}

void Session::HitTest(uint32_t node_id, const float ray_origin[3],
                      const float ray_direction[3], HitTestCallback callback) {
  ZX_DEBUG_ASSERT(session_);
  fuchsia::ui::gfx::vec3 ray_origin_vec;
  ray_origin_vec.x = ray_origin[0];
  ray_origin_vec.y = ray_origin[1];
  ray_origin_vec.z = ray_origin[2];

  fuchsia::ui::gfx::vec3 ray_direction_vec;
  ray_direction_vec.x = ray_direction[0];
  ray_direction_vec.y = ray_direction[1];
  ray_direction_vec.z = ray_direction[2];

  session_->HitTest(node_id, std::move(ray_origin_vec),
                    std::move(ray_direction_vec), std::move(callback));
}

void Session::HitTestDeviceRay(
    const float ray_origin[3], const float ray_direction[3],
    fuchsia::ui::scenic::Session::HitTestDeviceRayCallback callback) {
  ZX_DEBUG_ASSERT(session_);
  fuchsia::ui::gfx::vec3 ray_origin_vec;
  ray_origin_vec.x = ray_origin[0];
  ray_origin_vec.y = ray_origin[1];
  ray_origin_vec.z = ray_origin[2];

  fuchsia::ui::gfx::vec3 ray_direction_vec;
  ray_direction_vec.x = ray_direction[0];
  ray_direction_vec.y = ray_direction[1];
  ray_direction_vec.z = ray_direction[2];

  session_->HitTestDeviceRay(std::move(ray_origin_vec),
                             std::move(ray_direction_vec), std::move(callback));
}

void Session::Unbind() {
  ZX_DEBUG_ASSERT(session_);
  ZX_DEBUG_ASSERT(!session_handle_);
  session_handle_ = session_.Unbind();
  session_ = nullptr;
}

void Session::Rebind() {
  ZX_DEBUG_ASSERT(!session_);
  ZX_DEBUG_ASSERT(session_handle_);
  session_ = fuchsia::ui::scenic::SessionPtr(session_handle_.Bind());
  session_handle_ = nullptr;
}

void Session::OnScenicError(fidl::StringPtr error) {
  // TODO(SCN-903): replace fprintf with SDK-approved logging mechanism.  Also
  // remove "#include <stdio.h>".
  fprintf(stderr, "Session error: %s", error->c_str());
}

void Session::OnScenicEvent(
    fidl::VectorPtr<fuchsia::ui::scenic::Event> events) {
  if (event_handler_)
    event_handler_(std::move(events));
}

void Session::SetDebugName(const std::string& debug_name) {
  session_->SetDebugName(debug_name);
}

}  // namespace scenic
