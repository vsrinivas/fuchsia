// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file contains declarations internal to x86.
// Declarations visible outside of x86 belong in arch_perfmon.h.

#pragma once

#include <arch/x86.h>

void apic_pmi_interrupt_handler(x86_iframe_t *frame);
