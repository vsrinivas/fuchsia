// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "asan_impl.h"

#include <assert.h>

// This file provides the weak default definitions of sanitizer hook
// functions.  The purpose of these interfaces is for the sanitizer
// runtime library to override these definitions.

__WEAK void __sanitizer_startup_hook(int argc, char** argv, char** envp,
                                     void* stack_base, size_t stack_size) {
}

__WEAK void *__sanitizer_before_thread_create_hook(
    thrd_t thread, bool detached, const char* name,
    void* stack_base, size_t stack_size) {
    return NULL;
}

__WEAK void __sanitizer_thread_create_hook(void* hook, thrd_t th, int error) {
    assert(hook == NULL);
}

__WEAK void __sanitizer_thread_start_hook(void* hook, thrd_t self) {
    assert(hook == NULL);
}

__WEAK void __sanitizer_thread_exit_hook(void* hook, thrd_t self) {
    assert(hook == NULL);
}
