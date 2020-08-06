// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_REGISTER_UTIL_REGISTER_UTIL_H_
#define SRC_DEVICES_BIN_REGISTER_UTIL_REGISTER_UTIL_H_
#include <lib/zx/channel.h>

int run(int argc, const char** argv, zx::channel channel);

#endif  // SRC_DEVICES_BIN_REGISTER_UTIL_REGISTER_UTIL_H_
