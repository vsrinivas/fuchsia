// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_ZX_CHANNEL_PARAMS_H_
#define TOOLS_FIDLCAT_LIB_ZX_CHANNEL_PARAMS_H_

#include <lib/fit/function.h>
#include <stdint.h>

#include <memory>

#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/register.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace fidlcat {

// We should probably find a way to enforce this properly.
using zx_handle_t = uint32_t;

// Generic interface used when building the zx_channel_params needs to register
// a breakpoint to continue its work.  The prime example of this is when it has
// stopped on a zx_channel call, and needs to examine the results of the
// zx_channel call, so it steps forward until that call is finished.
class BreakpointRegisterer {
 public:
  virtual void Register(int64_t koid,
                        std::function<void(zxdb::Thread*)>&& cb) = 0;

  virtual void CreateNewBreakpoint(zxdb::BreakpointSettings& settings) = 0;
};

// Generic superclass for the parameters to a zx_channel read/write/call
// invocation.
class ZxChannelParams {
 public:
  friend class ZxChannelParamsBuilder;
  ZxChannelParams()
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

  ZxChannelParams(ZxChannelParams&& from)
      : handle_(from.handle_),
        options_(from.options_),
        bytes_(std::move(from.bytes_)),
        num_bytes_(from.num_bytes_),
        handles_(std::move(from.handles_)),
        num_handles_(from.num_handles_) {}

  // move semantics
  ZxChannelParams& operator=(ZxChannelParams&& from) {
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

 protected:
  zx_handle_t handle_;
  uint32_t options_;
  std::unique_ptr<uint8_t[]> bytes_;
  uint32_t num_bytes_;
  std::unique_ptr<zx_handle_t[]> handles_;
  uint32_t num_handles_;

  ZxChannelParams(zx_handle_t handle, uint32_t options,
                  std::unique_ptr<uint8_t[]> bytes, uint32_t num_bytes,
                  std::unique_ptr<zx_handle_t[]> handles, uint32_t num_handles)
      : handle_(handle),
        options_(options),
        bytes_(std::move(bytes)),
        num_bytes_(num_bytes),
        handles_(std::move(handles)),
        num_handles_(num_handles) {}

  bool IsComplete() const {
    // NB: The builder functions will attempt to get memory at any location,
    // including 0x0.  This means that nullptr is used exclusively to indicate
    // whether the bytes / handles are set.
    return (num_bytes_ == 0 || bytes_ != nullptr) &&
           (num_handles_ == 0 || handles_ != nullptr);
  }
};

// Can't use fit::function because zxdb doesn't use move semantics for its
// functions, and this needs to be captured as part of a std::function that gets
// passed to a zxdb function.
using ZxChannelCallback =
    std::function<void(const zxdb::Err&, const ZxChannelParams&)>;

namespace {

// Defined in zx_channel_params.cc
class CallingConventionDecoder;

}  // namespace

// Generic superclass for building params for zx_channel_read/write/call
// invocations.
class ZxChannelParamsBuilder {
 public:
  ZxChannelParamsBuilder()
      : once_(false), handle_(0), options_(0), num_bytes_(0), num_handles_(0) {}

  // Assuming that |thread| is stopped in a zx_channel_read, and that
  // |registers| is the set of registers for that thread, do what is necessary
  // to populate |params|, invoke fn, and continue.  Currently only works for
  // x64.
  //
  // Note on the lifetime of builders: It is the responsibility of a caller to
  // make sure that the builder has finished its work before deleting it.  This
  // may be accomplished by deleting it as the last action in the continuation
  // function |fn| - the continuation function is guaranteed to be the last
  // thing that is executed as part of the flow of this function .  It may not
  // be accomplished by deleting the builder directly after calling this method.
  void BuildZxChannelParamsAndContinue(fxl::WeakPtr<zxdb::Thread> thread,
                                       BreakpointRegisterer& registerer,
                                       ZxChannelCallback&& fn);

  virtual ~ZxChannelParamsBuilder() {}

 protected:
  virtual void BuildAndContinue(CallingConventionDecoder* fetcher,
                                fxl::WeakPtr<zxdb::Thread> thread,
                                const std::vector<zxdb::Register>& regs,
                                BreakpointRegisterer& registerer) = 0;

  const std::vector<zxdb::Register>* GetGeneralRegisters(
      fxl::WeakPtr<zxdb::Thread> thread, const zxdb::Err& err,
      const zxdb::RegisterSet& in_regs);

  void Cancel(const zxdb::Err& e);
  void Finalize();

  // Any errs that are propagated from the memory reads.
  zxdb::Err err_;

  ZxChannelCallback callback_;
  // We only execute callback_ once.  This guards that invariant.
  bool once_;

  // The values we need to construct the params object.
  zx_handle_t handle_;
  uint32_t options_;
  std::unique_ptr<uint8_t[]> bytes_;
  std::unique_ptr<zx_handle_t[]> handles_;
  uint32_t num_bytes_;
  uint32_t num_handles_;
};

class ZxChannelWriteParamsBuilder : public ZxChannelParamsBuilder {
 protected:
  virtual void BuildAndContinue(CallingConventionDecoder* fetcher,
                                fxl::WeakPtr<zxdb::Thread> thread,
                                const std::vector<zxdb::Register>& regs,
                                BreakpointRegisterer& registerer) override;
};

class ZxChannelReadParamsBuilder : public ZxChannelParamsBuilder {
 public:
  ZxChannelReadParamsBuilder()
      : thread_koid_(0),
        first_sp_(0),
        bytes_address_(0),
        handles_address_(0),
        actual_bytes_ptr_(0),
        actual_handles_ptr_(0),
        registerer_(nullptr) {}

  virtual ~ZxChannelReadParamsBuilder();

 protected:
  virtual void BuildAndContinue(CallingConventionDecoder* fetcher,
                                fxl::WeakPtr<zxdb::Thread> thread,
                                const std::vector<zxdb::Register>& regs,
                                BreakpointRegisterer& registerer) override;

 private:
  // This method steps the object through the state machine described by
  // PerThreadState, other than the stepping, which is controlled by
  // FinishChannelReadX86 and FinishChannelReadArm.
  void GetResultAndContinue(fxl::WeakPtr<zxdb::Thread> thread);

  void FinishChannelReadX86(fxl::WeakPtr<zxdb::Thread> thread);

  void FinishChannelReadArm(fxl::WeakPtr<zxdb::Thread> thread,
                            uint64_t link_register_contents);

  // This describes the possible states you can be in when you try to see the
  // effects of a zx_channel_read.  You start by executing in a breakpoint for
  // zx_channel_read.
  typedef enum {
    // Set up and execute a step
    STEPPING,
    // Check to see if you have stepped out of the zx_channel_read.
    CHECKING_STEP,
    // Read the contents of *actual_bytes
    READING_ACTUAL_BYTES,
    // Read the contents of *actual_handles
    READING_ACTUAL_HANDLES,
    // Filling in the bytes array based on the number of bytes read
    FILLING_IN_BYTES,
    // Filling in the handles array based on the number of handles read.
    FILLING_IN_HANDLES
  } PerThreadState;

  // The key value is the koid of a thread.
  static std::map<uint64_t, PerThreadState>& GetPerThreadState();

  // The koid of the stopped thread.
  uint64_t thread_koid_;

  // The stack pointer as of the invocation.
  uint64_t first_sp_;

  // The remote address containing the bytes.
  uint64_t bytes_address_;

  // The remote address containing the handles.
  uint64_t handles_address_;

  // The memory location of the actual bytes value
  uint64_t actual_bytes_ptr_;

  // The memory location of the actual handles value
  uint64_t actual_handles_ptr_;

  BreakpointRegisterer* registerer_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_ZX_CHANNEL_PARAMS_H_
