// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/mozart/command_dispatcher.h"

namespace mz {

CommandDispatcherContext::CommandDispatcherContext(Mozart* mozart,
                                                   Session* session)
    : mozart_(mozart), session_(session) {
  FXL_DCHECK(mozart_);
  FXL_DCHECK(session_);
}

CommandDispatcherContext::CommandDispatcherContext(
    CommandDispatcherContext&& context)
    : mozart_(context.mozart_), session_(context.session_) {
  FXL_DCHECK(mozart_);
  FXL_DCHECK(session_);
  context.mozart_ = nullptr;
  context.session_ = nullptr;
}

CommandDispatcher::CommandDispatcher(CommandDispatcherContext context)
    : context_(std::move(context)) {}

CommandDispatcher::~CommandDispatcher() = default;

}  // namespace mz
