// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>

#include <zircon/types.h>

// Waits for an incoming echo request on channel |handle|,
// parses the message, sends a reply on |handle|. Returns false if either
// channel handle is closed or any error occurs. Returns true if a reply is
// successfully sent.
bool serve_echo_request(zx_handle_t handle);
