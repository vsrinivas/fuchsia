// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/command_dispatcher.h"

namespace scenic {

CommandDispatcherContext::CommandDispatcherContext(Scenic* scenic,
                                                   Session* session)
    : scenic_(scenic), session_(session) {
  FXL_DCHECK(scenic_);
  FXL_DCHECK(session_);
}

CommandDispatcherContext::CommandDispatcherContext(
    CommandDispatcherContext&& context)
    : scenic_(context.scenic_), session_(context.session_) {
  FXL_DCHECK(scenic_);
  FXL_DCHECK(session_);
  context.scenic_ = nullptr;
  context.session_ = nullptr;
}

CommandDispatcher::CommandDispatcher(CommandDispatcherContext context)
    : context_(std::move(context)) {}

CommandDispatcher::~CommandDispatcher() = default;

TempSessionDelegate::TempSessionDelegate(CommandDispatcherContext context)
    : CommandDispatcher(std::move(context)) {}

}  // namespace scenic
