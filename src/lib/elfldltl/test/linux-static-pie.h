// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_TEST_LINUX_STATIC_PIE_H_
#define SRC_LIB_ELFLDLTL_TEST_LINUX_STATIC_PIE_H_

#include <cstdint>

// This is passed the starting value of the stack pointer as set by the kernel
// on execve.
extern "C" void StaticPieSetup(uintptr_t* start_sp);

#endif  // SRC_LIB_ELFLDLTL_TEST_LINUX_STATIC_PIE_H_
