// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

union node;

int process_launch(int argc, const char* const* argv, const char* path,
                   int index, zx_handle_t* process, zx_handle_t job,
                   zx_status_t* status_out, const char** errmsg);

zx_status_t process_subshell(union node* n, const char* const* envp,
                             zx_handle_t* process, zx_handle_t job,
                             int *fds, const char** errmsg);

// Waits for the process to terminate and returns the exit code for the process.
int process_await_termination(zx_handle_t process, zx_handle_t job, bool blocking);
