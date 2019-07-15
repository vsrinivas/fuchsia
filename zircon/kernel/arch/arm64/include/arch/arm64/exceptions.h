// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_EXCEPTIONS_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_EXCEPTIONS_H_

// Flags passed back from arm64_irq() to the calling assembler.
#define ARM64_IRQ_EXIT_THREAD_SIGNALED 1
#define ARM64_IRQ_EXIT_RESCHEDULE 2

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_EXCEPTIONS_H_
