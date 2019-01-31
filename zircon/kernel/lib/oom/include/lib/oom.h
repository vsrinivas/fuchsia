// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <sys/types.h>

// Called when the system is low on memory, |shortfall_bytes| below the memory
// redline.
typedef void(oom_lowmem_callback_t)(size_t shortfall_bytes);

// Initializes the out-of-memory system. If |enable| is true, starts the
// memory-watcher thread, which calls |lowmem_callback| when the PMM has less
// than |redline_bytes| free memory, sleeping for |sleep_duration_ns| between
// checks.
//
// If |enable| is false, the thread can be started manually using 'k oom start'.
// TODO(dbort): Add a programmatic way to start/stop the thread.
void oom_init(bool enable, uint64_t sleep_duration_ns, size_t redline_bytes,
              oom_lowmem_callback_t* lowmem_callback);
