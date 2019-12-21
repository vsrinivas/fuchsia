// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_PROVIDER_IMPL_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_PROVIDER_IMPL_H_

// This file defines a type "ArchProviderImpl" that maps to the concrete ArchProvider implementation
// for the current platform.

#if defined(__x86_64__)
#include "src/developer/debug/debug_agent/arch_provider_x64.h"
#elif defined(__aarch64__)
#include "src/developer/debug/debug_agent/arch_provider_arm64.h"
#else
#error
#endif

namespace debug_agent {

#if defined(__x86_64__)
using ArchProviderImpl = arch::ArchProviderX64;
#elif defined(__aarch64__)
using ArchProviderImpl = arch::ArchProviderArm64;
#endif

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_PROVIDER_IMPL_H_
