// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_CLI_SOCAT_H_
#define GARNET_BIN_GUEST_CLI_SOCAT_H_

#include <zircon/types.h>

void handle_socat_connect(uint32_t env_id, uint32_t cid, uint32_t port);
void handle_socat_listen(uint32_t env_id, uint32_t port);

#endif  // GARNET_BIN_GUEST_CLI_SOCAT_H_
