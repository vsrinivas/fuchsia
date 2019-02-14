// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

typedef struct {
    uint32_t vid;
    uint32_t pid;
    uint32_t did;
} display_driver_t;
