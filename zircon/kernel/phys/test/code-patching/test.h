// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_TEST_CODE_PATCHING_TEST_H_
#define ZIRCON_KERNEL_PHYS_TEST_CODE_PATCHING_TEST_H_

#include <lib/code-patching/code-patching.h>

constexpr code_patching::CaseId kAddOneCaseId = 17;

#if defined(__aarch64__)
constexpr size_t kAddOnePatchSize = 4;
#elif defined(__x86_64__)
constexpr size_t kAddOnePatchSize = 4;
#else
#error "unknown architecture"
#endif

#endif  // ZIRCON_KERNEL_PHYS_TEST_CODE_PATCHING_TEST_H_
