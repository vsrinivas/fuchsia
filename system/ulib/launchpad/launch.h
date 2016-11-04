// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <launchpad/launchpad.h>

#pragma GCC visibility push(hidden)

// If status is NO_ERROR, call launchpad_start.
// Otherwise, close all the handles and return status.
// Regardless, call launchpad_destroy.
mx_handle_t finish_launch(launchpad_t* lp, mx_status_t status,
                          mx_handle_t handles[], uint32_t handle_count);

// Retrive and cache the job handle passed via mxio_get_startup_handle().
mx_handle_t get_mxio_job(void);

#pragma GCC visibility pop
