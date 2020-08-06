// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// TODO(mcgrathr): See kernel/lib/arch/arm64/include/lib/arch/intrin.h
// for an explanation of this hackery.

#pragma GCC push_options
#pragma GCC target ("general-regs-only")
