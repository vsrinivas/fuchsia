// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_GUEST_SOCAT_H_
#define SRC_VIRTUALIZATION_BIN_GUEST_SOCAT_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/types.h>

void handle_socat_connect(uint32_t env_id, uint32_t cid, uint32_t port,
                          async::Loop* loop, sys::ComponentContext* context);
void handle_socat_listen(uint32_t env_id, uint32_t port, async::Loop* loop,
                         sys::ComponentContext* context);

#endif  // SRC_VIRTUALIZATION_BIN_GUEST_SOCAT_H_
