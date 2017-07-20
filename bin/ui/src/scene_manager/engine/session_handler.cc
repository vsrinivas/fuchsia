// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/engine/session_handler.h"

#include "apps/mozart/src/scene_manager/scene_manager_impl.h"
#include "lib/ftl/functional/make_copyable.h"

namespace scene_manager {

SessionHandler::SessionHandler(
    Engine* engine,
    SessionId session_id,
    ::fidl::InterfaceRequest<mozart2::Session> request,
    ::fidl::InterfaceHandle<mozart2::SessionListener> listener)
    : engine_(engine),
      session_(::ftl::MakeRefCounted<scene_manager::Session>(
          session_id,
          engine_,
          static_cast<ErrorReporter*>(this))) {
  FTL_DCHECK(engine);

  bindings_.set_on_empty_set_handler([this]() { BeginTearDown(); });

  Connect(std::move(request), std::move(listener));
}

SessionHandler::~SessionHandler() {}

void SessionHandler::EnqueueEvent(mozart2::EventPtr event) {
  FTL_DCHECK(event);
  buffered_events_.push_back(std::move(event));
}

void SessionHandler::FlushEvents(uint64_t presentation_time) {
  if (buffered_events_.empty())
    return;

  // TODO(MZ-215): We should remove |Connect| from the API so there's only
  // one listener to worry about.  Unfortunately InterfacePtrSet does
  // not support random access so we have to iterate over all of the
  // elements even though we passed the events to the first listener.
  // In current usage, there's only ever one listener anyhow so it's not worth
  // writing code to copy the event array.
  FTL_DCHECK(listeners_.size() <= 1u);
  listeners_.ForAllPtrs([
    events = std::move(buffered_events_), &presentation_time
  ](auto listener) mutable {
    listener->OnEvent(presentation_time, std::move(events));
  });
}

void SessionHandler::Enqueue(::fidl::Array<mozart2::OpPtr> ops) {
  // TODO: Add them all at once instead of iterating.  The problem
  // is that ::fidl::Array doesn't support this.  Or, at least reserve
  // enough space.  But ::fidl::Array doesn't support this, either.
  for (auto& op : ops) {
    buffered_ops_.push_back(std::move(op));
  }
}

void SessionHandler::Present(uint64_t presentation_time,
                             ::fidl::Array<mx::event> acquire_fences,
                             ::fidl::Array<mx::event> release_fences,
                             const PresentCallback& callback) {
  session_->ScheduleUpdate(presentation_time, std::move(buffered_ops_),
                           std::move(acquire_fences), std::move(release_fences),
                           callback);
}

void SessionHandler::Connect(
    ::fidl::InterfaceRequest<mozart2::Session> session,
    ::fidl::InterfaceHandle<mozart2::SessionListener> listener) {
  bindings_.AddBinding(this, std::move(session));
  if (listener) {
    listeners_.AddInterfacePtr(
        mozart2::SessionListenerPtr::Create(std::move(listener)));
  }
}

void SessionHandler::HitTest(uint32_t node_id,
                             mozart2::vec3Ptr ray_origin,
                             mozart2::vec3Ptr ray_direction,
                             const HitTestCallback& callback) {
  session_->HitTest(node_id, std::move(ray_origin), std::move(ray_direction),
                    callback);
}

void SessionHandler::ReportError(ftl::LogSeverity severity,
                                 std::string error_string) {
  switch (severity) {
    case ftl::LOG_INFO:
      FTL_LOG(INFO) << error_string;
      break;
    case ftl::LOG_WARNING:
      FTL_LOG(WARNING) << error_string;
      break;
    case ftl::LOG_ERROR:
      FTL_LOG(ERROR) << error_string;
      listeners_.ForAllPtrs(
          [&error_string](auto listener) { listener->OnError(error_string); });
      break;
    case ftl::LOG_FATAL:
      FTL_LOG(FATAL) << error_string;
      break;
    default:
      // Invalid severity.
      FTL_DCHECK(false);
  }
}

void SessionHandler::BeginTearDown() {
  engine_->TearDownSession(session_->id());
  FTL_DCHECK(!session_->is_valid());
}

void SessionHandler::TearDown() {
  bindings_.CloseAllBindings();
  listeners_.CloseAll();
  session_->TearDown();
}

}  // namespace scene_manager
