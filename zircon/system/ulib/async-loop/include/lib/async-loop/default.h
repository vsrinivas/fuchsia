// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_LOOP_DEFAULT_H_
#define LIB_ASYNC_LOOP_DEFAULT_H_

#include <lib/async-loop/loop.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

/// Simple config that when passed to async_loop_create will create a loop
/// that will automatically register itself as the default
/// dispatcher for the thread upon which it was created and will
/// automatically unregister itself when destroyed (which must occur on
/// the same thread).
extern const async_loop_config_t kAsyncLoopConfigAttachToCurrentThread;

/// Simple config that when passed to async_loop_create will create a loop
/// that is not registered to the current thread, but any threads created with
/// async_loop_start_thread will have the loop registered.
extern const async_loop_config_t kAsyncLoopConfigNoAttachToCurrentThread;

__END_CDECLS

#endif  // LIB_ASYNC_LOOP_LOOP_H_
