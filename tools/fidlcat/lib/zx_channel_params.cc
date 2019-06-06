// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zx_channel_params.h"

#include <string>
#include <vector>

#include "src/developer/debug/ipc/register_desc.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/step_thread_controller.h"
#include "src/lib/fxl/logging.h"

namespace fidlcat {

const std::vector<zxdb::Register>* ZxChannelParamsBuilder::GetGeneralRegisters(
    fxl::WeakPtr<zxdb::Thread> thread, const zxdb::Err& err,
    const zxdb::RegisterSet& in_regs) {
  if (!thread) {
    Cancel(zxdb::Err(zxdb::ErrType::kGeneral,
                     "Error reading registers: thread went away"));
    return nullptr;
  }
  if (!err.ok()) {
    Cancel(zxdb::Err(err.type(), "Error reading registers" + err.msg()));
    return nullptr;
  }
  auto regs_it =
      in_regs.category_map().find(debug_ipc::RegisterCategory::Type::kGeneral);
  if (regs_it == in_regs.category_map().end()) {
    Cancel(zxdb::Err(zxdb::ErrType::kGeneral, "Can't read registers"));
    return nullptr;
  }
  return &regs_it->second;
}

// This is called when we want to abort the current build.  Callers should not
// continue to try to build after it is called.
void ZxChannelParamsBuilder::Cancel(const zxdb::Err& e) {
  if (e.ok()) {
    err_ = zxdb::Err(zxdb::ErrType::kGeneral, "Canceled for unknown reason");
  } else {
    err_ = e;
  }
  Finalize();
}

// The last method to run, which invokes the ZxChannelCallback
void ZxChannelParamsBuilder::Finalize() {
  if (once_) {
    return;
  }
  once_ = true;
  if (!err_.ok()) {
    ZxChannelParams p;
    callback_(err_, p);
  } else {
    ZxChannelParams params =
        ZxChannelParams(handle_, options_, std::move(bytes_), num_bytes_,
                        std::move(handles_), num_handles_);
    zxdb::Err err;
    callback_(err, params);
  }
}

namespace {

// Helper function to convert a vector of bytes to a T.
template <typename T>
T GetValueFromBytes(const std::vector<uint8_t>& bytes) {
  T ret = 0;
  for (uint64_t i = 0; i < sizeof(ret) && i < bytes.size(); i++) {
    ret |= ((uint64_t)(bytes[i])) << (i * 8);
  }
  return ret;
}

// Helper function to convert an array of bytes to a T.
template <typename T>
T GetValueFromBytes(const T* bytes_t) {
  T ret = 0;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(bytes_t);
  constexpr size_t size = sizeof(T);
  for (uint64_t i = 0; i < sizeof(ret) && i < size; i++) {
    ret |= ((uint64_t)(bytes[i])) << (i * 8);
  }
  return ret;
}

// Helper function to convert the value in a register to a T
template <typename T>
T GetRegisterValue(const std::vector<zxdb::Register>& regs,
                   const debug_ipc::RegisterID id) {
  for (const zxdb::Register& reg : regs) {
    if (reg.id() == id) {
      return GetValueFromBytes<T>(reg.data());
    }
  }
  return 0;
}

// Grovels through the |dump| and constructs a local copy of the bytes into an
// array of type |T|, starting at |bytes_address| and continuing for |count|
// bytes.
template <typename T>
std::unique_ptr<T> MemoryDumpToArray(uint64_t bytes_address, uint32_t count,
                                     const zxdb::MemoryDump& dump) {
  std::unique_ptr<T> output_buffer = std::make_unique<T>(count);
  memset(reinterpret_cast<void*>(output_buffer.get()), 0, count);

  uint8_t* buffer_as_bytes = reinterpret_cast<uint8_t*>(output_buffer.get());
  size_t output_offset = 0;

  for (const debug_ipc::MemoryBlock& block : dump.blocks()) {
    if (!block.valid) {
      continue;
    }
    size_t block_offset = 0;
    if (block.address < bytes_address) {
      if (block.address + block.size < bytes_address) {
        continue;
      }
      block_offset = bytes_address - block.address;
    }
    while (block_offset < block.size &&
           output_offset < (sizeof(output_buffer.get()[0]) * count)) {
      buffer_as_bytes[output_offset] = block.data[block_offset];
      output_offset++;
      block_offset++;
    }
  }
  return output_buffer;
}

// Schedules an async task that gets the remote memory available via |thread|,
// located at |remote_address|, and going for |count| bytes.  The asynchronous
// task will invoke |callback| with the relevant error and the data retrieved.
template <typename T>
void GetMemoryAtAndThen(
    fxl::WeakPtr<zxdb::Thread> thread, uint64_t remote_address, uint64_t count,
    std::function<void(const zxdb::Err&, std::unique_ptr<T> data)> callback) {
  if (count == 0 || remote_address == 0) {
    zxdb::Err err;
    callback(err, {});
    return;
  }
  thread->GetProcess()->ReadMemory(
      remote_address, count,
      [callback = std::move(callback), remote_address, count](
          const zxdb::Err& err, zxdb::MemoryDump dump) mutable {
        std::unique_ptr<T> data;
        zxdb::Err e = err;
        if (err.ok()) {
          data = MemoryDumpToArray<T>(remote_address, count, dump);
        } else {
          std::string msg = "Failed to build parameters for syscall: ";
          msg.append(err.msg());
          e = zxdb::Err(err.type(), msg);
        }
        callback(e, std::move(data));
      });
}

// Abstract class that gets the parameters to a given functions, assuming you
// are in a breakpoint at the beginning of the function, and pass in the current
// registers.
class CallingConventionDecoder {
 public:
  explicit CallingConventionDecoder(const std::vector<zxdb::Register>& regs)
      : regs_(regs) {}

  // Fills out the arguments so that GetArgument can later be called, and then
  // runs the and_then function.
  virtual void PopulateArguments(
      fxl::WeakPtr<zxdb::Thread> thread, size_t arity,
      std::function<void(const zxdb::Err&)> and_then) = 0;

  // Getter: for argument at position i, return the value.
  template <typename T>
  T GetArgument(size_t position) {
    uint64_t tmp = GetUintArgument(position);
    return static_cast<T>(tmp);
  }

  // Gets the return value, per the calling conventions
  template <typename T>
  T GetReturnValue() {
    uint64_t tmp = GetResult();
    return static_cast<T>(tmp);
  }

  // Gets the link register, if it exists
  virtual std::optional<uint64_t> GetLinkRegister() const {
    return std::nullopt;
  }

  // Gets the stack pointer from the registers passed into the constructor.
  virtual uint64_t GetStackPointer() { return stack_pointer_; }

  CallingConventionDecoder() = delete;
  CallingConventionDecoder(const CallingConventionDecoder&) = delete;
  CallingConventionDecoder(CallingConventionDecoder&&) = delete;

  virtual ~CallingConventionDecoder() = default;

 protected:
  uint64_t GetUintArgument(size_t position) {
    FXL_DCHECK(position < args_.size()) << "Bad parameter to GetUintArgument";
    return args_[position];
  }

  virtual uint64_t GetResult() const = 0;

  std::vector<uint64_t> args_;
  const std::vector<zxdb::Register>& regs_;
  uint64_t stack_pointer_;
};

// X86 specialization of CallingConventionDecoder above.
class CallingConventionDecoderX86 : public CallingConventionDecoder {
 public:
  CallingConventionDecoderX86(const std::vector<zxdb::Register>& regs)
      : CallingConventionDecoder(regs) {
    stack_pointer_ =
        GetRegisterValue<uint64_t>(regs_, debug_ipc::RegisterID::kX64_rsp);
  }
  virtual void PopulateArguments(
      fxl::WeakPtr<zxdb::Thread> thread, size_t arity,
      std::function<void(const zxdb::Err&)> and_then) override {
    // The order of parameters in the System V AMD64 ABI we use, according to
    // Wikipedia:
    static debug_ipc::RegisterID param_regs[] = {
        debug_ipc::RegisterID::kX64_rdi, debug_ipc::RegisterID::kX64_rsi,
        debug_ipc::RegisterID::kX64_rdx, debug_ipc::RegisterID::kX64_rcx,
        debug_ipc::RegisterID::kX64_r8,  debug_ipc::RegisterID::kX64_r9};
    constexpr size_t param_regs_size =
        sizeof(param_regs) / sizeof(debug_ipc::RegisterID);
    args_.reserve(arity);
    size_t i = 0;
    for (; i < arity && i < param_regs_size; i++) {
      args_.push_back(GetRegisterValue<uint64_t>(regs_, param_regs[i]));
    }
    if (i == arity) {
      // No more arguments to resolve.  Returning.
      and_then(zxdb::Err());
      return;
    }

    // The remaining args are on the stack.  The first arg is rsp + 8, the
    // second is rsp + 16, and so on.
    size_t memory_amount_to_read = sizeof(uint64_t) * (arity - i);

    GetMemoryAtAndThen<uint64_t[]>(
        thread, stack_pointer_ + sizeof(uint64_t), memory_amount_to_read,
        [this, thread, current = i, arity, and_then = std::move(and_then)](
            const zxdb::Err& err, std::unique_ptr<uint64_t[]> data) {
          if (!thread || !err.ok()) {
            and_then(err);
            return;
          }
          if (data == nullptr) {
            and_then(zxdb::Err(zxdb::ErrType::kGeneral,
                               "Unable to read params for syscall"));
            return;
          }
          uint64_t* reg_arr = data.get();
          for (size_t i = current; i < arity; i++) {
            // TODO: This assumes that all of the parameters are 64-bit.  That's
            // broken if they aren't.
            args_.push_back(GetValueFromBytes<uint64_t>(reg_arr + i - current));
          }
          and_then(zxdb::Err());
        });
  };

  virtual uint64_t GetResult() const override {
    return GetRegisterValue<uint64_t>(regs_, debug_ipc::RegisterID::kX64_rax);
  }
};

// ARM specialization of CallingConventionDecoder above.
class CallingConventionDecoderArm : public CallingConventionDecoder {
 public:
  CallingConventionDecoderArm(const std::vector<zxdb::Register>& regs)
      : CallingConventionDecoder(regs) {
    stack_pointer_ =
        GetRegisterValue<uint64_t>(regs_, debug_ipc::RegisterID::kARMv8_sp);
  }

  virtual void PopulateArguments(
      fxl::WeakPtr<zxdb::Thread> thread, size_t arity,
      std::function<void(const zxdb::Err&)> and_then) override {
    FXL_CHECK(arity <= 8) << "Too many arguments for ARM call";
    // The order of parameters in the System V ARM64 ABI we use, according to
    // Wikipedia:
    static debug_ipc::RegisterID param_regs[] = {
        debug_ipc::RegisterID::kARMv8_x0, debug_ipc::RegisterID::kARMv8_x1,
        debug_ipc::RegisterID::kARMv8_x2, debug_ipc::RegisterID::kARMv8_x3,
        debug_ipc::RegisterID::kARMv8_x4, debug_ipc::RegisterID::kARMv8_x5,
        debug_ipc::RegisterID::kARMv8_x6, debug_ipc::RegisterID::kARMv8_x7};
    constexpr size_t param_regs_size =
        sizeof(param_regs) / sizeof(debug_ipc::RegisterID);
    args_.reserve(arity);
    size_t i = 0;
    for (; i < arity && i < param_regs_size; i++) {
      args_.push_back(GetRegisterValue<uint64_t>(regs_, param_regs[i]));
    }
    if (i == arity) {
      // No more arguments to resolve.  Returning.
      and_then(zxdb::Err());
      return;
    }
    FXL_NOTREACHED() << "More than 8 arguments, yet fewer.  How strange!";
  };

  virtual uint64_t GetResult() const override {
    return GetRegisterValue<uint64_t>(regs_, debug_ipc::RegisterID::kARMv8_x0);
  }

  virtual std::optional<uint64_t> GetLinkRegister() const override {
    return GetRegisterValue<uint64_t>(regs_, debug_ipc::RegisterID::kARMv8_lr);
  }
};

CallingConventionDecoder* GetFetcherFor(
    debug_ipc::Arch arch, const std::vector<zxdb::Register>& regs) {
  if (arch == debug_ipc::Arch::kX64) {
    return new CallingConventionDecoderX86(regs);
  } else if (arch == debug_ipc::Arch::kArm64) {
    return new CallingConventionDecoderArm(regs);
  }
  return nullptr;
}

}  // namespace

void ZxChannelParamsBuilder::BuildZxChannelParamsAndContinue(
    fxl::WeakPtr<zxdb::Thread> thread, BreakpointRegisterer& registerer,
    ZxChannelCallback&& fn) {
  callback_ = std::move(fn);

  std::vector<debug_ipc::RegisterCategory::Type> register_types = {
      debug_ipc::RegisterCategory::Type::kGeneral};

  thread->ReadRegisters(register_types, [this, thread, &registerer](
                                            const zxdb::Err& err,
                                            const zxdb::RegisterSet& in_regs) {
    const std::vector<zxdb::Register>* general_registers =
        GetGeneralRegisters(thread, err, in_regs);
    if (general_registers == nullptr) {
      return;
    }

    BuildAndContinue(GetFetcherFor(in_regs.arch(), *general_registers), thread,
                     *general_registers, registerer);
  });
}

// Assuming that |thread| is stopped in a zx_channel_write, and that |regs| is
// the set of registers for that thread, and that both are on a connected x64
// device, do what is necessary to populate |params| and pass them to the
// callback.
//
// This remains pretty brittle WRT the order of parameters to zx_channel_write
// and the calling conventions.  The zx_channel_write parameters may change;
// we'll update as appropriate.
void ZxChannelWriteParamsBuilder::BuildAndContinue(
    CallingConventionDecoder* fetcher, fxl::WeakPtr<zxdb::Thread> thread,
    const std::vector<zxdb::Register>& regs, BreakpointRegisterer& registerer) {
  if (fetcher == nullptr) {
    Cancel(zxdb::Err(zxdb::ErrType::kCanceled, "Unknown arch"));
    return;
  }
  fetcher->PopulateArguments(
      thread, 6, [this, fetcher, thread](const zxdb::Err& err) {
        std::unique_ptr<CallingConventionDecoder> f(fetcher);
        if (!err.ok()) {
          Cancel(err);
          return;
        }
        if (!thread) {
          Cancel(zxdb::Err(zxdb::ErrType::kGeneral,
                           "Error reading params: thread went away"));
          return;
        }
        zx_handle_t handle = fetcher->GetArgument<zx_handle_t>(0);
        uint32_t options = fetcher->GetArgument<uint32_t>(1);
        uint64_t bytes_address = fetcher->GetArgument<uint64_t>(2);
        uint32_t num_bytes = fetcher->GetArgument<uint32_t>(3);
        uint64_t handles_address = fetcher->GetArgument<uint64_t>(4);
        uint32_t num_handles = fetcher->GetArgument<uint32_t>(5);

        handle_ = handle;
        options_ = options;
        num_bytes_ = num_bytes;
        num_handles_ = num_handles;

        // Note that the lambdas capture |this|.  In typical use, |this| will be
        // deleted by Finalize().  See the docs on
        // ZxChannelParamsBuilder::BuildZxChannelParamsAndContinue for more
        // detail.
        GetMemoryAtAndThen<uint8_t[]>(
            thread, bytes_address, num_bytes,
            [this](const zxdb::Err& err, std::unique_ptr<uint8_t[]> data) {
              bytes_ = std::move(data);
              if ((num_handles_ == 0 || handles_ != nullptr) || !err.ok()) {
                Finalize();
              }
            });

        GetMemoryAtAndThen<zx_handle_t[]>(
            thread, handles_address, num_handles * sizeof(zx_handle_t),
            [this](const zxdb::Err& err, std::unique_ptr<zx_handle_t[]> data) {
              handles_ = std::move(data);
              if ((num_bytes_ == 0 || bytes_ != nullptr) || !err.ok()) {
                Finalize();
              }
            });

        if (num_handles == 0 && num_bytes == 0) {
          Finalize();
        }
      });
}

std::map<uint64_t, ZxChannelReadParamsBuilder::PerThreadState>&
ZxChannelReadParamsBuilder::GetPerThreadState() {
  static std::map<uint64_t, ZxChannelReadParamsBuilder::PerThreadState> the_map;
  return the_map;
}

ZxChannelReadParamsBuilder::~ZxChannelReadParamsBuilder() {
  GetPerThreadState().erase(thread_koid_);
}

void ZxChannelReadParamsBuilder::GetResultAndContinue(
    fxl::WeakPtr<zxdb::Thread> thread) {
  PerThreadState state = GetPerThreadState()[thread_koid_];

  // Read the filled in values for actual_bytes and actual_handles, then read
  // the memory at those locations, and then finish.
  if (state == PerThreadState::READING_ACTUAL_BYTES) {
    // actual_bytes_ptr_ is allowed to be null.
    if (actual_bytes_ptr_ == 0) {
      num_bytes_ = 0;
      GetPerThreadState()[thread_koid_] =
          PerThreadState::READING_ACTUAL_HANDLES;
      GetResultAndContinue(thread);
    } else {
      GetMemoryAtAndThen<uint32_t>(
          thread, actual_bytes_ptr_, sizeof(uint32_t),
          [this, thread](const zxdb::Err& err, std::unique_ptr<uint32_t> data) {
            if (!thread || !err.ok()) {
              Cancel(err);
              return;
            }
            if (data != nullptr) {
              num_bytes_ = *data;
              GetPerThreadState()[thread_koid_] =
                  PerThreadState::READING_ACTUAL_HANDLES;
            } else {
              Cancel(zxdb::Err(zxdb::ErrType::kGeneral,
                               "Malformed zx_channel_read call"));
              return;
            }
            GetResultAndContinue(thread);
          });
    }
  } else if (state == PerThreadState::READING_ACTUAL_HANDLES) {
    // actual_handles_ptr_ is allowed to be null.
    if (actual_handles_ptr_ == 0) {
      num_handles_ = 0;
      GetPerThreadState()[thread_koid_] = PerThreadState::FILLING_IN_BYTES;
      GetResultAndContinue(thread);
    } else {
      GetMemoryAtAndThen<uint32_t>(
          thread, actual_handles_ptr_, sizeof(uint32_t),
          [this, thread](const zxdb::Err& err, std::unique_ptr<uint32_t> data) {
            if (!thread || !err.ok()) {
              Cancel(err);
              return;
            }
            num_handles_ = *data;
            GetPerThreadState()[thread_koid_] =
                PerThreadState::FILLING_IN_BYTES;
            GetResultAndContinue(thread);
          });
    }
  } else if (state == PerThreadState::FILLING_IN_BYTES) {
    if (num_bytes_ == 0) {
      GetPerThreadState()[thread_koid_] = PerThreadState::FILLING_IN_HANDLES;
      GetResultAndContinue(thread);
      return;
    }
    GetMemoryAtAndThen<uint8_t[]>(
        thread, bytes_address_, num_bytes_,
        [this, thread](const zxdb::Err& err, std::unique_ptr<uint8_t[]> data) {
          if (!thread || !err.ok()) {
            Cancel(err);
            return;
          }
          bytes_ = std::move(data);
          if (num_handles_ == 0 || !err_.ok()) {
            Finalize();
          } else {
            GetPerThreadState()[thread_koid_] =
                PerThreadState::FILLING_IN_HANDLES;
            GetResultAndContinue(thread);
          }
        });
  } else if (state == PerThreadState::FILLING_IN_HANDLES) {
    if (num_handles_ == 0) {
      Finalize();
      return;
    }
    GetMemoryAtAndThen<zx_handle_t[]>(
        thread, handles_address_, num_handles_ * sizeof(zx_handle_t),
        [this, thread](const zxdb::Err& err,
                       std::unique_ptr<zx_handle_t[]> data) {
          if (!thread || !err.ok()) {
            Cancel(err);
            return;
          }
          handles_ = std::move(data);
          Finalize();
        });
  }
}

// Assuming that |thread| is stopped in a zx_channel_read, and that |regs| is
// the set of registers for that thread, and that both are on a connected
// device, do what is necessary to populate |params| and pass them to the
// callback.
//
// This remains pretty brittle WRT the order of parameters to zx_channel_read
// and calling conventions.  Those things aren't likely to change, but
// if they did, we'd have to update this.
void ZxChannelReadParamsBuilder::BuildAndContinue(
    CallingConventionDecoder* fetcher, fxl::WeakPtr<zxdb::Thread> thread,
    const std::vector<zxdb::Register>& regs, BreakpointRegisterer& registerer) {
  if (fetcher == nullptr) {
    Cancel(zxdb::Err(zxdb::ErrType::kCanceled, "Unknown arch"));
    return;
  }
  std::optional<uint64_t> link_register = fetcher->GetLinkRegister();

  thread_koid_ = thread->GetKoid();
  registerer_ = &registerer;

  once_ = false;
  first_sp_ = fetcher->GetStackPointer();

  fetcher->PopulateArguments(
      thread, 8, [this, thread, fetcher, link_register](const zxdb::Err& err) {
        std::unique_ptr<CallingConventionDecoder> f(fetcher);
        if (!err.ok()) {
          Cancel(err);
          return;
        }
        if (!thread) {
          Cancel(zxdb::Err(zxdb::ErrType::kGeneral,
                           "Error reading params: thread went away"));
          return;
        }
        handle_ = fetcher->GetArgument<zx_handle_t>(0);
        options_ = fetcher->GetArgument<uint32_t>(1);
        bytes_address_ = fetcher->GetArgument<uint64_t>(2);
        handles_address_ = fetcher->GetArgument<uint64_t>(3);
        // The num_bytes and num_handles values are not useful, and are only
        // included here for documentation purposes:
        // num_bytes_ = fetcher->GetArgument(4);
        // num_handles_ = fetcher->GetArgument(5);
        actual_bytes_ptr_ = fetcher->GetArgument<uint64_t>(6);
        actual_handles_ptr_ = fetcher->GetArgument<uint64_t>(7);
        GetPerThreadState()[thread_koid_] = PerThreadState::STEPPING;
        if (link_register) {
          FinishChannelReadArm(thread, *link_register);
        } else {
          FinishChannelReadX86(thread);
        }
      });
}

// Advance until the stack pointer increases (i.e., the stack frame has popped)
void ZxChannelReadParamsBuilder::FinishChannelReadX86(
    fxl::WeakPtr<zxdb::Thread> thread) {
  PerThreadState state = GetPerThreadState()[thread_koid_];

  if (state == PerThreadState::STEPPING) {
    // Then we step...
    auto controller = std::make_unique<zxdb::StepThreadController>(
        zxdb::StepMode::kSourceLine);
    controller->set_stop_on_no_symbols(false);
    GetPerThreadState()[thread_koid_] = PerThreadState::CHECKING_STEP;
    thread->ContinueWith(
        std::move(controller), [this, thread](const zxdb::Err& err) {
          if (!thread || !err.ok()) {
            Cancel(err);
            return;
          }
          if (thread) {
            registerer_->Register(thread->GetKoid(),
                                  [this, thread](zxdb::Thread* t) {
                                    // TODO: I think the post-stepping stack
                                    // pointer may be in *thread somewhere.
                                    FinishChannelReadX86(thread);
                                  });
          }
        });
  } else if (state == PerThreadState::CHECKING_STEP) {
    // ... and we continue to step until the stack pointer increases, indicating
    // that we've exited the method.
    std::vector<debug_ipc::RegisterCategory::Type> register_types = {
        debug_ipc::RegisterCategory::Type::kGeneral};
    thread->ReadRegisters(
        register_types,
        [this, thread](const zxdb::Err& err, const zxdb::RegisterSet& in_regs) {
          auto general_registers = GetGeneralRegisters(thread, err, in_regs);
          if (general_registers == nullptr) {
            return;
          }
          std::unique_ptr<CallingConventionDecoder> fetcher(
              GetFetcherFor(in_regs.arch(), *general_registers));
          FXL_CHECK(fetcher != nullptr) << "No architecture found";

          // See if the stack pointer has regressed, if not, step some more.
          uint64_t stack_pointer = fetcher->GetStackPointer();
          if (stack_pointer > first_sp_) {
            int64_t result = fetcher->GetReturnValue<uint64_t>();
            if (result < 0) {
              std::string message =
                  "aborted zx_channel_read (errno=" + std::to_string(result) +
                  ")";
              err_ = zxdb::Err(zxdb::ErrType::kGeneral, message);
              Finalize();
            } else {
              GetPerThreadState()[thread->GetKoid()] =
                  PerThreadState::READING_ACTUAL_BYTES;
              GetResultAndContinue(thread);
            }
          } else {
            GetPerThreadState()[thread->GetKoid()] = PerThreadState::STEPPING;
            FinishChannelReadX86(thread);
          }
        });
  }
}

// Advance to wherever the link register says the return location of the
// zx_channel_read is.
void ZxChannelReadParamsBuilder::FinishChannelReadArm(
    fxl::WeakPtr<zxdb::Thread> thread, uint64_t link_register_contents) {
  PerThreadState state = GetPerThreadState()[thread_koid_];
  if (state == PerThreadState::STEPPING) {
    zxdb::BreakpointSettings settings;
    settings.enabled = true;
    settings.stop_mode = zxdb::BreakpointSettings::StopMode::kThread;
    settings.type = debug_ipc::BreakpointType::kSoftware;
    settings.location.address = link_register_contents;
    settings.location.type = zxdb::InputLocation::Type::kAddress;
    settings.scope = zxdb::BreakpointSettings::Scope::kThread;
    settings.scope_thread = &(*thread);
    settings.scope_target = thread->GetProcess()->GetTarget();
    settings.one_shot = true;
    registerer_->CreateNewBreakpoint(settings);
    registerer_->Register(thread->GetKoid(), [this, thread](zxdb::Thread* t) {
      std::vector<debug_ipc::RegisterCategory::Type> register_types = {
          debug_ipc::RegisterCategory::Type::kGeneral};
      thread->ReadRegisters(
          register_types, [this, thread](const zxdb::Err& err,
                                         const zxdb::RegisterSet& in_regs) {
            auto general_registers = GetGeneralRegisters(thread, err, in_regs);
            if (general_registers == nullptr) {
              return;
            }
            std::unique_ptr<CallingConventionDecoder> fetcher(
                GetFetcherFor(in_regs.arch(), *general_registers));
            FXL_CHECK(fetcher != nullptr) << "No architecture found";

            int64_t result = fetcher->GetReturnValue<int64_t>();
            if (result < 0) {
              std::string message =
                  "aborted zx_channel_read (errno=" + std::to_string(result) +
                  ")";
              err_ = zxdb::Err(zxdb::ErrType::kGeneral, message);
              Finalize();
            } else {
              GetPerThreadState()[thread_koid_] =
                  PerThreadState::READING_ACTUAL_BYTES;
              GetResultAndContinue(thread);
            }
          });
    });
    thread->Continue();
  }
}

}  // namespace fidlcat
