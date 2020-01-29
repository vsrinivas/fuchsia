// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_GUEST_LAUNCH_H_
#define SRC_VIRTUALIZATION_BIN_GUEST_LAUNCH_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

void handle_launch(int argc, const char** argv, async::Loop* loop,
                   fuchsia::virtualization::GuestConfig guest_config,
                   sys::ComponentContext* context);

#endif  // SRC_VIRTUALIZATION_BIN_GUEST_LAUNCH_H_
