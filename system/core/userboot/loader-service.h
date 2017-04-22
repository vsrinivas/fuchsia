// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#pragma GCC visibility push(hidden)

#include <magenta/types.h>
#include <stddef.h>

struct bootfs;

// Handle loader-service RPCs on channel until there are no more.
// Consumes the channel.
void loader_service(mx_handle_t log, struct bootfs*, mx_handle_t channel);

#pragma GCC visibility pop
