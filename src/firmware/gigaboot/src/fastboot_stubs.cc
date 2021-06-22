// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fastboot.h"

// This has to live here rather than in host/stubs.c due to dependence on
// the fb_poll_next_action enum.
fb_poll_next_action fb_poll(fb_bootimg_t*) { return POLL; }
