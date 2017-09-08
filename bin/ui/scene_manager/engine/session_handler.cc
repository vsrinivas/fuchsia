// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/engine/session_handler.h"

#include "garnet/bin/ui/scene_manager/scene_manager_impl.h"
#include "lib/ftl/functional/make_copyable.h"

namespace scene_manager {

SessionHandler::SessionHandler(
    Engine* engine,
    SessionId session_id,
    ::fidl::InterfaceRequest<scenic::Session> request,
    ::fidl::InterfaceHandle<scenic::SessionListener> listener)
    : engine_(engine),
      session_(::ftl::MakeRefCounted<scene_manager::Session>(
          session_id,
          engine_,
          this,
          static_cast<ErrorReporter*>(this))),
      listener_(::fidl::InterfacePtr<scenic::SessionListener>::Create(
          std::move(listener))) {
  FTL_DCHECK(engine);

  bindings_.set_on_empty_set_handler([this]() { BeginTearDown(); });
  bindings_.AddBinding(this, std::move(request));
}

SessionHandler::~SessionHandler() {}

void SessionHandler::SendEvents(::fidl::Array<scenic::EventPtr> events) {
  if (listener_) {
    listener_->OnEvent(std::move(events));
  }
}

void SessionHandler::Enqueue(::fidl::Array<scenic::OpPtr> ops) {
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

void SessionHandler::HitTest(uint32_t node_id,
                             scenic::vec3Ptr ray_origin,
                             scenic::vec3Ptr ray_direction,
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
      if (listener_) {
        listener_->OnError(error_string);
      }
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
  listener_.reset();
  session_->TearDown();
}

}  // namespace scene_manager
