// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_BOOTLOADER_INCLUDE_STRINGS_H_
#define ZIRCON_BOOTLOADER_INCLUDE_STRINGS_H_

#include <stddef.h>

int strcasecmp(const char* s1, const char* s2);
int strncasecmp(const char* s1, const char* s2, size_t len);

#endif  // ZIRCON_BOOTLOADER_INCLUDE_STRINGS_H_
