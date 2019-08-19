// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/loop.h>
#include <lib/async/default.h>

const async_loop_config_t kAsyncLoopConfigAttachToCurrentThread = {
    .make_default_for_current_thread = true,
    .default_accessors = {
        .getter = async_get_default_dispatcher,
        .setter = async_set_default_dispatcher,
    }};

const async_loop_config_t kAsyncLoopConfigNoAttachToCurrentThread = {
    .make_default_for_current_thread = false,
    .default_accessors = {
        .getter = async_get_default_dispatcher,
        .setter = async_set_default_dispatcher,
    }};
