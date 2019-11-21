// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scenic/command_dispatcher.h"

#include "src/ui/scenic/lib/scenic/scenic.h"
#include "src/ui/scenic/lib/scenic/session.h"

namespace scenic_impl {

CommandDispatcherContext::CommandDispatcherContext(Scenic* scenic, Session* session)
    : CommandDispatcherContext(scenic, session, session->id()) {}

CommandDispatcherContext::CommandDispatcherContext(Scenic* scenic, Session* session, SessionId id)
    : scenic_(scenic), session_(session), session_id_(id) {
  if (session) {
    FXL_DCHECK(session->id() == id);
  }
}

CommandDispatcherContext::CommandDispatcherContext(CommandDispatcherContext&& context)
    : CommandDispatcherContext(context.scenic_, context.session_, context.session_id_) {
  auto& other_scenic = const_cast<Scenic*&>(context.scenic_);
  auto& other_session = const_cast<Session*&>(context.session_);
  auto& other_session_id = const_cast<SessionId&>(context.session_id_);
  other_scenic = nullptr;
  other_session = nullptr;
  other_session_id = 0;
}

void CommandDispatcherContext::KillSession() { scenic_->CloseSession(session_id()); }

CommandDispatcher::CommandDispatcher(CommandDispatcherContext context)
    : context_(std::move(context)) {}

CommandDispatcher::~CommandDispatcher() = default;

TempSessionDelegate::TempSessionDelegate(CommandDispatcherContext context)
    : CommandDispatcher(std::move(context)) {}

}  // namespace scenic_impl
