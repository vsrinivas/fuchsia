// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_ctx.h"

DriverCtx::DriverCtx() {}

zx_status_t DriverCtx::Init() { return ZX_OK; }
