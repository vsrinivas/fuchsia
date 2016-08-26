// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef __ASSERT_H
#define __ASSERT_H

#include <magenta/assert.h>
#include <magenta/compiler.h>
#include <debug.h>

#define assert(x) DEBUG_ASSERT(x)

#endif
