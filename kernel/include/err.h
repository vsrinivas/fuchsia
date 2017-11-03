// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#ifndef __ASSEMBLER__
#include <zircon/types.h> // for zx_status_t
#endif

#include <zircon/errors.h>

// TODO: This is used primarily by class drivers which are obsolete.
// Re-examine when those are removed.
#define ZX_ERR_NOT_CONFIGURED (-501)

// MOVE to kernel internal used for thread teardown
#define ZX_ERR_INTERNAL_INTR_KILLED (-502)
