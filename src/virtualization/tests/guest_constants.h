// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_GUEST_CONSTANTS_H_
#define SRC_VIRTUALIZATION_TESTS_GUEST_CONSTANTS_H_

#include <string_view>

// Linux kernel command line params for additional serial debug logs during boot.
constexpr std::string_view kLinuxKernelSerialDebugCmdline[] = {
// Add early UART output from kernel.
#if __aarch64__
    "earlycon=pl011,0x808300000",
#elif __x86_64__
    "earlycon=uart,io,0x3f8",
#else
#error Unknown architecture.
#endif

    // Tell Linux to keep the console in polling mode instead of trying to switch
    // to a real UART driver. The latter assumes a working transmit interrupt,
    // but we don't implement one yet.
    //
    // TODO(fxbug.dev/48616): Ideally, Machina's UART would support IRQs allowing
    // us to just use the full UART driver.
    "keep_bootcon",

    // Tell Linux to not try and use the UART as a console, but use the virtual
    // console tty0 instead.
    //
    // TODO(fxbug.dev/48616): If Machina's UART had full IRQ support, using
    // ttyS0 as a console would be fine.
    "console=tty0",
};

#endif  // SRC_VIRTUALIZATION_TESTS_GUEST_CONSTANTS_H_
