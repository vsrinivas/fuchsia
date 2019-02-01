// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_CLI_BALLOON_H_
#define GARNET_BIN_GUEST_CLI_BALLOON_H_

#include "lib/component/cpp/startup_context.h"

void handle_balloon(uint32_t env_id, uint32_t cid, uint32_t num_pages,
                    component::StartupContext* context);

void handle_balloon_stats(uint32_t env_id, uint32_t cid,
                          component::StartupContext* context);

#endif  // GARNET_BIN_GUEST_CLI_BALLOON_H_
