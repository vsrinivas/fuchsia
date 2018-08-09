// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include "lib/fxl/memory/ref_counted.h"

namespace zxdb {

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
  virtual void GetRegisterAsync(
      int dwarf_register_number,
      std::function<void(bool success, uint64_t value)> callback) = 0;

  // Request to retrieve a memory block from the debugged process. On success,
  // the implementation will call the callback with the retrieved data pointer.
  // The size of the buffer provided the callback will be the same size
  // requested in the input parameter. The implementation will make the data
  // valid for the duration of the callback, but it may be destroyed afterward.
  //
  // On failure (if all or part of the memory is unreadable), the callback will
  // be issued with a null pointer.
  virtual void GetMemoryAsync(
      uint64_t address, uint32_t size,
      std::function<void(const uint8_t* data)> callback) = 0;

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(SymbolDataProvider);
  virtual ~SymbolDataProvider() = default;
};

}  // namespace zxdb
