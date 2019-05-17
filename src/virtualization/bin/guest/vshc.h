// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_GUEST_VSHC_H_
#define SRC_VIRTUALIZATION_BIN_GUEST_VSHC_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#include <optional>

std::pair<int, int> init_tty(void);

void reset_tty(void);

void handle_vsh(std::optional<uint32_t> env_id, std::optional<uint32_t> cid,
                std::optional<uint32_t> port, async::Loop* loop,
                sys::ComponentContext* context);

#endif  // SRC_VIRTUALIZATION_BIN_GUEST_VSHC_H_
