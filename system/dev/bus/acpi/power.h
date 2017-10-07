// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

void poweroff(void);
void reboot(void);
zx_status_t suspend_to_ram(void);
