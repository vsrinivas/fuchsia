// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_KEYBOARD_H_
#define ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_KEYBOARD_H_

#include <lib/cbuf.h>

void platform_init_keyboard(Cbuf* buffer);

int platform_read_key(char* c);

// Reboot the system via the keyboard, returns on failure.
void pc_keyboard_reboot(void);

#endif  // ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_KEYBOARD_H_
