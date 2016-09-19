// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>

__BEGIN_CDECLS

#pragma GCC visibility push(hidden)

// Examine the next message to be read from the pipe, and yield
// the data size and number of handles in that message.
mx_status_t mxr_message_size(mx_handle_t msg_pipe,
                             uint32_t* nbytes, uint32_t* nhandles);

#pragma GCC visibility pop

__END_CDECLS
