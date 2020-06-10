// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_UARCH_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_UARCH_H_

bool arm64_uarch_needs_spectre_v2_mitigation();
void arm64_uarch_do_spectre_v2_mitigation();

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_UARCH_H_
