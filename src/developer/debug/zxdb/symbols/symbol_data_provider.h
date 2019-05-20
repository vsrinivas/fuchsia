// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_DATA_PROVIDER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_DATA_PROVIDER_H_

#include <stdint.h>

#include <functional>
#include <optional>
#include <vector>

#include "src/developer/debug/ipc/protocol.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

class Err;

// This interface is how the debugger backend provides memory and register data
// to the symbol system to evaluate expressions.
//
// By default, this class returns no information. In this form it can be used
// to evaluate expressions in contexts without a running process. To access
// data, most callers will want to use the implementation associated with a
// frame or a process.
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

  virtual debug_ipc::Arch GetArch();

  // Request for synchronous register data. If the value is not synchronously
  // availble, the *value will always be a nullopt.
  //
  // A return value of false means that the value is not known synchronously.
  // In this case, GetRegisterAsync should be called to retrieve the value.
  //
  // In the synchronous case, we could have the value, but we could also know
  // that the value is not known (e.g. when that register was not saved for the
  // stack frame). The *value will reflect this when the return value is true.
  virtual bool GetRegister(debug_ipc::RegisterID id,
                           std::optional<uint64_t>* value);

  // Request for register data with an asynchronous callback. The callback will
  // be issued when the register data is available.
  //
  // The success parameter indicates whether the operation was successful. If
  // the register is not available now (maybe the thread is running), success
  // will be set to false. When the register value contains valid data, success
  // will indicate true.
  virtual void GetRegisterAsync(debug_ipc::RegisterID id,
                                GetRegisterCallback callback);

  // Synchronously returns the frame base pointer if possible. As with
  // GetRegister, if this is not available the implementation should call
  // GetFrameBaseAsync().
  //
  // The frame base is the DW_AT_frame_base for the current function. Often
  // this will be the "base pointer" register in the CPU, but could be other
  // registers, especially if compiled without full stack frames. Getting this
  // value may involve evaluating another DWARF expression which may or may not
  // be asynchronous.
  virtual std::optional<uint64_t> GetFrameBase();

  // Asynchronous version of GetFrameBase.
  virtual void GetFrameBaseAsync(GetRegisterCallback callback);

  // Request to retrieve a memory block from the debugged process. On success,
  // the implementation will call the callback with the retrieved data pointer.
  //
  // It will read valid memory up to the maximum. It will do short reads if it
  // encounters invalid memory, so the result may be shorter than requested
  // or empty (if the first byte is invalid).
  virtual void GetMemoryAsync(uint64_t address, uint32_t size,
                              GetMemoryCallback callback);

  // Asynchronously writes to the given memory. The callback will be issued
  // when the write is complete.
  virtual void WriteMemory(uint64_t address, std::vector<uint8_t> data,
                           std::function<void(const Err&)> cb);

 protected:
  FRIEND_MAKE_REF_COUNTED(SymbolDataProvider);
  FRIEND_REF_COUNTED_THREAD_SAFE(SymbolDataProvider);

  SymbolDataProvider() = default;
  virtual ~SymbolDataProvider() = default;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_DATA_PROVIDER_H_
