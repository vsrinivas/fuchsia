// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file is #include'd multiple times with the DEFINE_OPTION macro defined.
// See kernel/lib/boot-options/README.md for details.
//
// These are the riscv64-specific boot options.

DEFINE_OPTION("kernel.riscv64.phys.mmu", bool, phys_mmu, {true}, R"""(
This enables use of the MMU and caches during the kernel's early boot phase.
)""")
