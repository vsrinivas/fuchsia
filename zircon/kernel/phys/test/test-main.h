// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_TEST_TEST_MAIN_H_
#define ZIRCON_KERNEL_PHYS_TEST_TEST_MAIN_H_

#include <lib/arch/ticks.h>

#include "../main.h"
#include "../symbolize.h"

int TestMain(void*, arch::EarlyTicks) PHYS_SINGLETHREAD;

#endif  // ZIRCON_KERNEL_PHYS_TEST_TEST_MAIN_H_
