// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <err.h>
#include <platform.h>

/*
 * default implementations of these routines, if the platform code
 * chooses not to implement.
 */

__WEAK void platform_init_mmu_mappings() {
}

__WEAK void platform_early_init() {
}

__WEAK void platform_init() {
}

__WEAK void platform_quiesce() {
}

__WEAK void platform_panic_start() {
}

__WEAK void* platform_get_ramdisk(size_t* size) {
    *size = 0;
    return NULL;
}
