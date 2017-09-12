// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/tasks/message_loop_handler.h"

namespace fsl {

MessageLoopHandler::~MessageLoopHandler() {}

void MessageLoopHandler::OnHandleReady(mx_handle_t handle,
                                       mx_signals_t pending,
                                       uint64_t count) {}

void MessageLoopHandler::OnHandleError(mx_handle_t handle, mx_status_t error) {}

}  // namespace fsl
