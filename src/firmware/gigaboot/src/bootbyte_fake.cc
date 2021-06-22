// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "bootbyte.h"

namespace {

unsigned char state = RTC_BOOT_DEFAULT;

}  // namespace

unsigned char bootbyte_read(void) { return state; }

void bootbyte_clear(void) { state = RTC_BOOT_DEFAULT; }

void bootbyte_set_normal(void) { state = RTC_BOOT_NORMAL; }

void bootbyte_set_recovery(void) { state = RTC_BOOT_RECOVERY; }

void bootbyte_set_bootloader(void) { state = RTC_BOOT_BOOTLOADER; }

void bootbyte_set_for_test(unsigned char value) { state = value; }
