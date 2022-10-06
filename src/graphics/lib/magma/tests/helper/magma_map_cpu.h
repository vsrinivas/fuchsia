// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_TESTS_HELPER_MAGMA_MAP_CPU_H_
#define SRC_GRAPHICS_LIB_MAGMA_TESTS_HELPER_MAGMA_MAP_CPU_H_

#include <stddef.h>

#include <magma/magma.h>

namespace magma {

// Maps a magma buffer into the process address space using the OS specific syscall.
bool MapCpuHelper(magma_buffer_t buffer, size_t offset, size_t length, void** addr_out);

// Unmaps a magma buffer from the process address space using the OS specific syscall.
bool UnmapCpuHelper(void* addr, size_t length);

}  // namespace magma

#endif  // SRC_GRAPHICS_LIB_MAGMA_TESTS_HELPER_MAGMA_MAP_CPU_H_
