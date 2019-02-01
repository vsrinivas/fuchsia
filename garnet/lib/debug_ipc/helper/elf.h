// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stdio.h>

#include <functional>
#include <string>

#if defined(__Fuchsia__)
#include <lib/zx/process.h>
#endif

namespace debug_ipc {

// Extracts the build ID from some ELF data. Returns the empty string on
// failure.
//
// The parameter is a function that that implements an fread-like interface to
// read from the ELF data. This allows the extractor to handle in-memory and
// on-disk version. It returns true on success, false on failure (failures
// include all partial reads). The offset is relative to the beginning of the
// ELF file.
std::string ExtractBuildID(
    std::function<bool(uint64_t offset, void* buffer, size_t length)> read_fn);

// This variant extracts the build ID from the given file.
std::string ExtractBuildID(FILE* file);

#if defined(__Fuchsia__)
// This variant extracts the build ID from an ELF file mapped into memory for
// the given process at the given location.
std::string ExtractBuildID(const zx::process& process, uint64_t base);
#endif

}  // namespace debug_ipc
