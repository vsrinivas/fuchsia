// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_CLI_BALLOON_H_
#define GARNET_BIN_GUEST_CLI_BALLOON_H_

#include <lib/sys/cpp/component_context.h>

void handle_balloon(uint32_t env_id, uint32_t cid, uint32_t num_pages,
                    sys::ComponentContext* context);

void handle_balloon_stats(uint32_t env_id, uint32_t cid,
                          sys::ComponentContext* context);

#endif  // GARNET_BIN_GUEST_CLI_BALLOON_H_
