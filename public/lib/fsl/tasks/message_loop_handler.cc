// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/tasks/message_loop_handler.h"

namespace fsl {

MessageLoopHandler::~MessageLoopHandler() {}

void MessageLoopHandler::OnHandleReady(zx_handle_t handle,
                                       zx_signals_t pending,
                                       uint64_t count) {}

void MessageLoopHandler::OnHandleError(zx_handle_t handle, zx_status_t error) {}

}  // namespace fsl
