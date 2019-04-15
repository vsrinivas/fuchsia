// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_ZX_CHANNEL_PARAMS_H_
#define TOOLS_FIDLCAT_LIB_ZX_CHANNEL_PARAMS_H_

#include <lib/fit/function.h>
#include <stdint.h>

#include <memory>

#include "src/developer/debug/zxdb/client/register.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace fidlcat {

// We should probably find a way to enforce this properly.
using zx_handle_t = uint32_t;

class ZxChannelWriteParams;

// Can't use fit::function because zxdb doesn't use move semantics for its
// functions, and this needs to be captured as part of a std::function that gets
// passed to a zxdb function.
using ZxChannelWriteCallback =
    std::function<void(const zxdb::Err&, const ZxChannelWriteParams&)>;

// Represents the parameters to a zx_channel_write call.
class ZxChannelWriteParams {
 public:
  ZxChannelWriteParams()
      : handle_(0),
        options_(0),
        bytes_(nullptr),
        num_bytes_(0),
        handles_(nullptr),
        num_handles_(0) {}

  zx_handle_t GetHandle() const { return handle_; }
  uint32_t GetOptions() const { return options_; }
  const std::unique_ptr<uint8_t[]>& GetBytes() const { return bytes_; }
  uint32_t GetNumBytes() const { return num_bytes_; }
  const std::unique_ptr<zx_handle_t[]>& GetHandles() const { return handles_; }
  uint32_t GetNumHandles() const { return num_handles_; }

  ZxChannelWriteParams(ZxChannelWriteParams&& from)
      : handle_(from.handle_),
        options_(from.options_),
        bytes_(std::move(from.bytes_)),
        num_bytes_(from.num_bytes_),
        handles_(std::move(from.handles_)),
        num_handles_(from.num_handles_) {}

  // move semantics
  ZxChannelWriteParams& operator=(ZxChannelWriteParams&& from) {
    if (this != &from) {
      handle_ = from.handle_;
      options_ = from.options_;
      bytes_ = std::move(from.bytes_);
      num_bytes_ = from.num_bytes_;
      handles_ = std::move(from.handles_);
      num_handles_ = from.num_handles_;
    }
    return *this;
  }

  // Assuming that |thread| is stopped in a zx_channel_write, and that
  // |registers| is the set of registers for that thread, do what is necessary
  // to populate |params| and invoke fn.  Currently only works for x64.
  static void BuildZxChannelWriteParamsAndContinue(
      fxl::WeakPtr<zxdb::Thread> thread, const zxdb::RegisterSet& registers,
      ZxChannelWriteCallback&& fn);

 private:
  zx_handle_t handle_;
  uint32_t options_;
  std::unique_ptr<uint8_t[]> bytes_;
  uint32_t num_bytes_;
  std::unique_ptr<zx_handle_t[]> handles_;
  uint32_t num_handles_;

  ZxChannelWriteParams(zx_handle_t handle, uint32_t options,
                       std::unique_ptr<uint8_t[]> bytes, uint32_t num_bytes,
                       std::unique_ptr<zx_handle_t[]> handles,
                       uint32_t num_handles)
      : handle_(handle),
        options_(options),
        bytes_(std::move(bytes)),
        num_bytes_(num_bytes),
        handles_(std::move(handles)),
        num_handles_(num_handles) {}

  static void BuildX86AndContinue(fxl::WeakPtr<zxdb::Thread> thread,
                                  const std::vector<zxdb::Register>& regs,
                                  ZxChannelWriteCallback&& fn);

  bool IsComplete() {
    // NB: The builder functions will attempt to get memory at any location,
    // including 0x0.  This means that nullptr is used exclusively to indicate
    // whether the bytes / handles are set.
    return (num_bytes_ == 0 || bytes_ != nullptr) &&
           (num_handles_ == 0 || handles_ != nullptr);
  }
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_ZX_CHANNEL_PARAMS_H_
