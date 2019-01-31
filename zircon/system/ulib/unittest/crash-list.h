// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/syscalls.h>

#include <unittest/unittest.h>

__BEGIN_CDECLS

/** Returns a new list for registering processes and threads expected to crash. */
crash_list_t crash_list_new(void);

/**
 * Registers the process or thread as expected to crash.
 */
void crash_list_register(crash_list_t crash_list, zx_handle_t handle);

/**
 * Lookup the given koid and returns the process or thread handle, or
 * ZX_HANDLE_INVALID if no match was found.
 */
zx_handle_t crash_list_lookup_koid(crash_list_t crash_list,
                                   zx_koid_t koid);

/**
 * Deletes the node with the given koid and returns the process or thread handle, or
 * ZX_HANDLE_INVALID if no match was found.
 */
zx_handle_t crash_list_delete_koid(crash_list_t crash_list,
                                   zx_koid_t koid);

/**
 * Deletes the list. Returns whether any elements were deleted.
 */
bool crash_list_delete(crash_list_t crash_list);

__END_CDECLS
