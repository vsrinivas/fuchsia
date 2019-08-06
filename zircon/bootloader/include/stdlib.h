// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_BOOTLOADER_INCLUDE_STDLIB_H_
#define ZIRCON_BOOTLOADER_INCLUDE_STDLIB_H_

#include <stddef.h>

int atoi(const char* nptr);
long atol(const char* nptr);
long long atoll(const char* nptr);

#endif  // ZIRCON_BOOTLOADER_INCLUDE_STDLIB_H_
