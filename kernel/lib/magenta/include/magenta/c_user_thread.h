// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

// N.B. This file is included by C code.

__BEGIN_CDECLS

// This is the interface for LK's thread_owner_name to return the
// name of the thread's process.
// This function is defined in user_thread.cpp.
const char* magenta_thread_process_name(void* user_thread);

__END_CDECLS
