// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/scene/client/session.h"

#include "apps/mozart/lib/scene/session_helpers.h"
#include "lib/ftl/logging.h"

namespace mozart {
namespace client {

Session::Session(mozart2::SessionPtr session) : session_(std::move(session)) {
  FTL_DCHECK(session_);
}

Session::~Session() {
  FTL_DCHECK(resource_count_ == 0)
      << "Some resources outlived the session: " << resource_count_;
}

uint32_t Session::AllocResourceId() {
  uint32_t resource_id = next_resource_id_++;
  FTL_DCHECK(resource_id);
  resource_count_++;
  return resource_id;
}

void Session::ReleaseResource(uint32_t resource_id) {
  resource_count_--;
  Enqueue(mozart::NewReleaseResourceOp(resource_id));
}

void Session::Enqueue(mozart2::OpPtr op) {
  FTL_DCHECK(op);
  ops_.push_back(std::move(op));
}

void Session::EnqueueAcquireFence(mx::event fence) {
  FTL_DCHECK(fence);
  acquire_fences_.push_back(std::move(fence));
}

void Session::EnqueueReleaseFence(mx::event fence) {
  FTL_DCHECK(fence);
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

}  // namespace client
}  // namespace mozart
