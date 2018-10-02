// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/debug.h>
#include <stdint.h>
#include <zircon/compiler.h>

// clang-format off

// binding info
#define SIERRA_VID        0x1199
#define EM7565_PID        0x9091
#define EM7565_PHY_ID     0x11
#define QMI_INTERFACE_NUM 8

// port info
#define CHANNEL_MSG 1
#define INTERRUPT_MSG 2

