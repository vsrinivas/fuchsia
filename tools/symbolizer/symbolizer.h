// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SYMBOLIZER_SYMBOLIZER_H_
#define TOOLS_SYMBOLIZER_SYMBOLIZER_H_

#include <iostream>
#include <string_view>

namespace symbolizer {

// This is the core logic of the symbolizer. The implementation is separated from the interface here
// for better testing.
class Symbolizer {
 public:
  enum class AddressType {
    kUnknown,
    kReturnAddress,   // :ra suffix
    kProgramCounter,  // :pc suffix
  };
  virtual ~Symbolizer() = default;

  // The following 4 functions correspond to the 4 markup tags we support right now.
  // Check //docs/reference/kernel/symbolizer_markup.md for details.

  // Resets the internal state and starts processing the stack trace for a new process.
  virtual void Reset() = 0;

  // Adds a module to the current process, indexed by id.
  virtual void Module(uint64_t id, std::string_view name, std::string_view build_id) = 0;

  // Associates a memory region with the module indexed by its id.
  virtual void MMap(uint64_t address, uint64_t size, uint64_t module_id,
                    uint64_t module_offset) = 0;

  // Represents one frame in the backtrace. We'll output the symbolized content for each frame.
  virtual void Backtrace(int frame_id, uint64_t address, AddressType type,
                         std::string_view message) = 0;
};

}  // namespace symbolizer

#endif  // TOOLS_SYMBOLIZER_SYMBOLIZER_H_
