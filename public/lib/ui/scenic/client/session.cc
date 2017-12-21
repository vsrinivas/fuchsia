// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/scenic/client/session.h"

#include "lib/ui/scenic/fidl_helpers.h"
#include "lib/fxl/logging.h"

namespace scenic_lib {

Session::Session(
    scenic::SessionPtr session,
    fidl::InterfaceRequest<scenic::SessionListener> session_listener)
    : session_(std::move(session)), session_listener_binding_(this) {
  FXL_DCHECK(session_);
  if (session_listener.is_pending())
    session_listener_binding_.Bind(std::move(session_listener));
}

Session::Session(scenic::SceneManager* scene_manager)
    : session_listener_binding_(this) {
  FXL_DCHECK(scene_manager);
  scene_manager->CreateSession(session_.NewRequest(),
                               session_listener_binding_.NewBinding());
}

Session::~Session() {
  FXL_DCHECK(resource_count_ == 0)
      << "Some resources outlived the session: " << resource_count_;
}

uint32_t Session::AllocResourceId() {
  uint32_t resource_id = next_resource_id_++;
  FXL_DCHECK(resource_id);
  resource_count_++;
  return resource_id;
}

void Session::ReleaseResource(uint32_t resource_id) {
  resource_count_--;
  Enqueue(NewReleaseResourceOp(resource_id));
}

void Session::Enqueue(scenic::OpPtr op) {
  FXL_DCHECK(op);
  ops_.push_back(std::move(op));
}

void Session::EnqueueAcquireFence(zx::event fence) {
  FXL_DCHECK(fence);
  acquire_fences_.push_back(std::move(fence));
}

void Session::EnqueueReleaseFence(zx::event fence) {
  FXL_DCHECK(fence);
  release_fences_.push_back(std::move(fence));
}

void Session::Flush() {
  if (!ops_.empty())
    session_->Enqueue(std::move(ops_));
}

void Session::Present(uint64_t presentation_time, PresentCallback callback) {
  Flush();

  if (acquire_fences_.is_null())
    acquire_fences_.resize(0u);
  if (release_fences_.is_null())
    release_fences_.resize(0u);
  session_->Present(presentation_time, std::move(acquire_fences_),
                    std::move(release_fences_), std::move(callback));
}

void Session::HitTest(uint32_t node_id,
                      const float ray_origin[3],
                      const float ray_direction[3],
                      HitTestCallback callback) {
  auto ray_origin_vec = scenic::vec3::New();
  ray_origin_vec->x = ray_origin[0];
  ray_origin_vec->y = ray_origin[1];
  ray_origin_vec->z = ray_origin[2];

  auto ray_direction_vec = scenic::vec3::New();
  ray_direction_vec->x = ray_direction[0];
  ray_direction_vec->y = ray_direction[1];
  ray_direction_vec->z = ray_direction[2];

  session_->HitTest(node_id, std::move(ray_origin_vec),
                    std::move(ray_direction_vec), std::move(callback));
}

void Session::OnError(const fidl::String& error) {
  FXL_LOG(ERROR) << "Session error: " << error;
}

void Session::OnEvent(fidl::Array<scenic::EventPtr> events) {
  if (event_handler_)
    event_handler_(std::move(events));
}

}  // namespace scenic_lib
