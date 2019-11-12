// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_MANAGEMENT_LAUNCH_H_
#define FS_MANAGEMENT_LAUNCH_H_

#include <zircon/types.h>

__BEGIN_CDECLS

// Callback that will launch the requested program.  |argv[argc]| is guaranteed
// to be accessible and set to nullptr.
typedef zx_status_t (*LaunchCallback)(int argc, const char** argv, zx_handle_t* hnd, uint32_t* ids,
                                      size_t len);

// Creates no logs, waits for process to terminate.
zx_status_t launch_silent_sync(int argc, const char** argv, zx_handle_t* handles, uint32_t* types,
                               size_t len);
// Creates no logs, does not wait for process to terminate.
zx_status_t launch_silent_async(int argc, const char** argv, zx_handle_t* handles, uint32_t* types,
                                size_t len);
// Creates stdio logs, waits for process to terminate.
zx_status_t launch_stdio_sync(int argc, const char** argv, zx_handle_t* handles, uint32_t* types,
                              size_t len);
// Creates stdio logs, does not wait for process to terminate.
zx_status_t launch_stdio_async(int argc, const char** argv, zx_handle_t* handles, uint32_t* types,
                               size_t len);
// Creates kernel logs, does not wait for process to terminate.
zx_status_t launch_logs_async(int argc, const char** argv, zx_handle_t* handles, uint32_t* types,
                              size_t len);

__END_CDECLS

#endif  // FS_MANAGEMENT_LAUNCH_H_
