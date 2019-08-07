// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TASK_UTILS_DUMP_THREADS_H_
#define TASK_UTILS_DUMP_THREADS_H_

#include <zircon/compiler.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

/*
 * Dump all the threads in a process.
 * pid: process id.
 * process: handle to the process.
 * verbosity_level: verbosity can be tuned.
 */
void dump_all_threads(uint64_t pid, zx_handle_t process, uint8_t verbosity_level);

#endif  // TASK_UTILS_DUMP_THREADS_H_
