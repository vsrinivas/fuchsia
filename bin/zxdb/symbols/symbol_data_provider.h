// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <functional>
#include <optional>
#include <vector>

#include "lib/fxl/memory/ref_counted.h"

namespace zxdb {

class Err;

// This interface is how the debugger backend provides memory and register data
// to the symbol system to evaluate expressions.
//
// Registers are the most commonly accessed data type and they are often
// available synchronously. So the interface provides a synchronous main
// register getter function and a fallback asynchronous one. They are separated
// to avoid overhead of closure creation in the synchronous case, and to
// avoid having a callback that's never issued.
//
// This object is reference counted since evaluating a DWARF expression is
// asynchronous.
class SymbolDataProvider
    : public fxl::RefCountedThreadSafe<SymbolDataProvider> {
 public:
  using GetMemoryCallback =
      std::function<void(const Err&, std::vector<uint8_t>)>;
  using GetRegisterCallback = std::function<void(const Err&, uint64_t)>;

  // Special register numbers (normal DWARF registers are never negative).
  // These will be mapped to the corresponding platform-specific registers for
  // the current platform.
  //
  // These are guaranteed available synchronously. If the synchronous getter
  // returns failure for them, it means the register isn't available in the
  // current context.
  static constexpr int kRegisterIP = -1;

  // Request for synchronous register data. If the register data can be provided
  // synchronously, the data will be put into the output parameter and this
  // function will return true.
  //
  // If synchronous data is not available, this function will return false. The
  // output will be unmodified. The caller should call GetRegisterAsync().
  virtual bool GetRegister(int dwarf_register_number, uint64_t* output) = 0;

  // Request for register data with an asynchronous callback. The callback will
  // be issued when the register data is available.
  //
  // The success parameter indicates whether the operation was successful. If
  // the register is not available now (maybe the thread is running), success
  // will be set to false. When the register value contains valid data, success
  // will indicate true.
  virtual void GetRegisterAsync(int dwarf_register_number,
                                GetRegisterCallback callback) = 0;

  // Synchronously returns the frame base pointer if possible. As with
  // GetRegister, if this is not available the implementation should call
  // GetFrameBaseAsync().
  //
  // The frame base is the DW_AT_frame_base for the current function. Often
  // this will be the "base pointer" register in the CPU, but could be other
  // registers, especially if compiled without full stack frames. Getting this
  // value may involve evaluating another DWARF expression which may or may not
  // be asynchronous.
  virtual std::optional<uint64_t> GetFrameBase() = 0;

  // Asynchronous version of GetFrameBase.
  virtual void GetFrameBaseAsync(GetRegisterCallback callback) = 0;

  // Request to retrieve a memory block from the debugged process. On success,
  // the implementation will call the callback with the retrieved data pointer.
  //
  // It will read valid memory up to the maximum. It will do short reads if it
  // encounters invalid memory, so the result may be shorter than requested
  // or empty (if the first byte is invalid).
  virtual void GetMemoryAsync(uint64_t address, uint32_t size,
                              GetMemoryCallback callback) = 0;

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(SymbolDataProvider);
  virtual ~SymbolDataProvider() = default;
};

}  // namespace zxdb
