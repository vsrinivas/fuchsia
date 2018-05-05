// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_CLI_SERVICE_H_
#define GARNET_BIN_GUEST_CLI_SERVICE_H_

#include <fuchsia/cpp/guest.h>

extern guest::GuestControllerPtr g_guest_controller;

guest::GuestController* connect(uint32_t guest_id);

#endif  // GARNET_BIN_GUEST_CLI_SERVICE_H_
