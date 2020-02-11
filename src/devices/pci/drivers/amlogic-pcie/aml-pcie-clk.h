// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/mmio/mmio.h>
#include <zircon/types.h>

zx_status_t PllSetRate(ddk::MmioBuffer* mmio);
