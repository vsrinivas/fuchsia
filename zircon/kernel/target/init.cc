// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <target.h>

#include <debug.h>
#include <err.h>
#include <zircon/compiler.h>

/*
 * default implementations of these routines, if the target code
 * chooses not to implement.
 */

__WEAK void target_early_init() {
}

__WEAK void target_init() {
}

__WEAK void target_quiesce() {
}
