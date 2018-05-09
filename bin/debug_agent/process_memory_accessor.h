// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace debug_agent {

// An abstract class that represents the ability to read and write from a
// process' memory. This allows such tasks to be mocked out for testing.
class ProcessMemoryAccessor {
 public:
  virtual zx_status_t ReadProcessMemory(
      uintptr_t address, void* buffer, size_t len, size_t* actual) = 0;

  virtual zx_status_t WriteProcessMemory(
      uintptr_t address, const void* buffer, size_t len, size_t* actual) = 0;
};

}  // namespace debug_agent
