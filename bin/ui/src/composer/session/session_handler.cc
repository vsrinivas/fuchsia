// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/composer/session/session_handler.h"

#include "apps/mozart/src/composer/composer_impl.h"
#include "lib/ftl/functional/make_copyable.h"

namespace mozart {
namespace composer {

SessionHandler::SessionHandler(
    ComposerImpl* composer,
    SessionId session_id,
    ::fidl::InterfaceRequest<mozart2::Session> request)
    : composer_(composer),
      session_(::ftl::MakeRefCounted<composer::Session>(
          session_id,
          composer,
          static_cast<ErrorReporter*>(this))),
      binding_(this, std::move(request)) {
  FTL_DCHECK(composer_);
  binding_.set_connection_error_handler([this]() {
    FTL_DCHECK(session_->is_valid());
    composer_->TearDownSession(session_->id());
    FTL_DCHECK(!session_->is_valid());
  });
}

SessionHandler::~SessionHandler() {}

void SessionHandler::Enqueue(::fidl::Array<mozart2::OpPtr> ops) {
  // TODO: Add them all at once instead of iterating.  The problem
  // is that ::fidl::Array doesn't support this.  Or, at least reserve
  // enough space.  But ::fidl::Array doesn't support this, either.
  for (auto& op : ops) {
    buffered_ops_.push_back(std::move(op));
  }
}

void SessionHandler::Present(::fidl::Array<mx::event> wait_events,
                             ::fidl::Array<mx::event> signal_events) {
  auto update = std::make_unique<SessionUpdate>(
      SessionUpdate{session_, std::move(buffered_ops_), std::move(wait_events),
                    std::move(signal_events)});
  composer_->ApplySessionUpdate(std::move(update));
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
      break;
    case ftl::LOG_FATAL:
      FTL_LOG(FATAL) << error_string;
      break;
    default:
      // Invalid severity.
      FTL_DCHECK(false);
  }
}

void SessionHandler::TearDown() {
  session_->TearDown();
  binding_.Close();
}

}  // namespace composer
}  // namespace mozart
