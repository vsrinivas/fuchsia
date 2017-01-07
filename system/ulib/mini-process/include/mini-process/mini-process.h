// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS

// Create and run a minimal process with one thread running a busy loop. Does not
// require a host binary.

mx_status_t start_mini_process(mx_handle_t job, mx_handle_t* process, mx_handle_t* thread);

__END_CDECLS
