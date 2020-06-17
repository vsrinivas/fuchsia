// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_WAIT_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_WAIT_H_

// The definitions of Thread and WaitQueue are pretty entertwined. For
// now, at least, we've chosen to place their definitions in the same
// header so they can see each other's internals.
//
// We include thread.h here, then. In the future, we may be able to
// move the definition of WaitQueue back here.
#include <kernel/thread.h>

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_WAIT_H_
