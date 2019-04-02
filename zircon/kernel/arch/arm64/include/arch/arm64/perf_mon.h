// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file contains declarations internal to arm64.
// Declarations visible outside of arm64 belong in arch_perfmon.h.

#pragma once

#include <arch/arm64.h>

void arm64_pmi_interrupt_handler(const iframe_short_t* frame);
