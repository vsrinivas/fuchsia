// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off
#define OVL_STA                 0x0000
#define OVL_INTEN               0x0004
#define OVL_INTSTA              0x0008
#define OVL_EN                  0x000C
#define OVL_LX_ADDR(x)          (0x0F40 + 0x20 * x)
