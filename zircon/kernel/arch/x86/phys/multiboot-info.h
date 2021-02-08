// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_PHYS_MULTIBOOT_INFO_H_
#define ZIRCON_KERNEL_ARCH_X86_PHYS_MULTIBOOT_INFO_H_

#include <ktl/byte.h>
#include <ktl/span.h>
#include <ktl/string_view.h>

// These are collected from the Multiboot info by InitMemory().

extern ktl::string_view gMultibootBootloader;
extern ktl::string_view gMultibootCmdline;
extern ktl::span<ktl::byte> gMultibootModule;

#endif  // ZIRCON_KERNEL_ARCH_X86_PHYS_MULTIBOOT_INFO_H_
