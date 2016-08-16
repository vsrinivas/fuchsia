// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>

#include <magenta/types.h>

// Waits for an incoming echo request on message pipe |handle|,
// parses the message, sends a reply on |handle|. Returns false if either
// message pipe handle is closed or any error occurs. Returns true if a reply is
// successfully sent.
bool serve_echo_request(mx_handle_t handle);
