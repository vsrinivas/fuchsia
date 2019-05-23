// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zx_channel_params.h"

#include <vector>

#include "src/developer/debug/ipc/register_desc.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/step_thread_controller.h"
#include "src/lib/fxl/logging.h"

namespace fidlcat {

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
    if (!thread) {
      Cancel(zxdb::Err(zxdb::ErrType::kGeneral,
                       "Error reading registers: thread went away"));
    }
    if (!err.ok()) {
      Cancel(zxdb::Err(err.type(), "Error reading registers" + err.msg()));
    }

    auto regs_it = in_regs.category_map().find(
        debug_ipc::RegisterCategory::Type::kGeneral);
    if (regs_it == in_regs.category_map().end()) {
      Cancel(zxdb::Err(zxdb::ErrType::kInput,
                       "zx_channel function params not found?"));
      return;
    }

    if (in_regs.arch() == debug_ipc::Arch::kX64) {
      BuildX86AndContinue(thread, regs_it->second, registerer);
    } else if (in_regs.arch() == debug_ipc::Arch::kArm64) {
      BuildArmAndContinue(thread, regs_it->second, registerer);
    } else {
      Cancel(zxdb::Err(zxdb::ErrType::kCanceled, "Unknown arch"));
    }
  });
}

// Assuming that |thread| is stopped in a zx_channel_write, and that |regs| is
// the set of registers for that thread, and that both are on a connected x64
// device, do what is necessary to populate |params| and pass them to the
// callback.
//
// This remains pretty brittle WRT the order of parameters to zx_channel_write
// and the x86 calling conventions.  The zx_channel_write parameters may change;
// we'll update as appropriate.
void ZxChannelWriteParamsBuilder::BuildX86AndContinue(
    fxl::WeakPtr<zxdb::Thread> thread, const std::vector<zxdb::Register>& regs,
    BreakpointRegisterer& registerer) {
  // The order of parameters in the System V AMD64 ABI we use, according to
  // Wikipedia:
  zx_handle_t handle =
      GetRegisterValue<zx_handle_t>(regs, debug_ipc::RegisterID::kX64_rdi);
  uint32_t options =
      GetRegisterValue<uint32_t>(regs, debug_ipc::RegisterID::kX64_rsi);
  uint64_t bytes_address =
      GetRegisterValue<uint64_t>(regs, debug_ipc::RegisterID::kX64_rdx);
  uint32_t num_bytes =
      GetRegisterValue<uint32_t>(regs, debug_ipc::RegisterID::kX64_rcx);
  uint64_t handles_address =
      GetRegisterValue<uint64_t>(regs, debug_ipc::RegisterID::kX64_r8);
  uint32_t num_handles =
      GetRegisterValue<uint32_t>(regs, debug_ipc::RegisterID::kX64_r9);

  handle_ = handle;
  options_ = options;
  num_bytes_ = num_bytes;
  num_handles_ = num_handles;

  BuildCommonAndContinue(thread, handle, options, bytes_address,
                         handles_address, num_bytes, num_handles);
}

// Assuming that |thread| is stopped in a zx_channel_write, and that |regs| is
// the set of registers for that thread, and that both are on a connected ARM
// device, do what is necessary to populate |params| and pass them to the
// callback.
//
// This remains pretty brittle WRT the order of parameters to zx_channel_write
// and the ARM calling conventions.  The zx_channel_write parameters may change;
// we'll update as appropriate.
void ZxChannelWriteParamsBuilder::BuildArmAndContinue(
    fxl::WeakPtr<zxdb::Thread> thread, const std::vector<zxdb::Register>& regs,
    BreakpointRegisterer& registerer) {
  // The order of parameters in the System V AMD64 ABI we use, according to
  // Wikipedia:
  zx_handle_t handle =
      GetRegisterValue<zx_handle_t>(regs, debug_ipc::RegisterID::kARMv8_x0);
  uint32_t options =
      GetRegisterValue<uint32_t>(regs, debug_ipc::RegisterID::kARMv8_x1);
  uint64_t bytes_address =
      GetRegisterValue<uint64_t>(regs, debug_ipc::RegisterID::kARMv8_x2);
  uint32_t num_bytes =
      GetRegisterValue<uint32_t>(regs, debug_ipc::RegisterID::kARMv8_x3);
  uint64_t handles_address =
      GetRegisterValue<uint64_t>(regs, debug_ipc::RegisterID::kARMv8_x4);
  uint32_t num_handles =
      GetRegisterValue<uint32_t>(regs, debug_ipc::RegisterID::kARMv8_x5);

  handle_ = handle;
  options_ = options;
  num_bytes_ = num_bytes;
  num_handles_ = num_handles;

  BuildCommonAndContinue(thread, handle, options, bytes_address,
                         handles_address, num_bytes, num_handles);
}

void ZxChannelWriteParamsBuilder::BuildCommonAndContinue(
    fxl::WeakPtr<zxdb::Thread> thread, zx_handle_t handle, uint32_t options,
    uint64_t bytes_address, uint64_t handles_address, uint32_t num_bytes,
    uint32_t num_handles) {
  // Note that the lambdas capture |this|.  In typical use, |this| will be
  // deleted by Finalize().  See the docs on
  // ZxChannelParamsBuilder::BuildZxChannelParamsAndContinue for more detail.
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
}

std::map<uint64_t, ZxChannelReadParamsBuilder::PerThreadState>&
ZxChannelReadParamsBuilder::GetPerThreadState() {
  static std::map<uint64_t, ZxChannelReadParamsBuilder::PerThreadState> the_map;
  return the_map;
}

ZxChannelReadParamsBuilder::~ZxChannelReadParamsBuilder() {
  GetPerThreadState().erase(thread_koid_);
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
          if (!thread || !err.ok()) {
            Cancel(err);
            return;
          }
          auto regs_it = in_regs.category_map().find(
              debug_ipc::RegisterCategory::Type::kGeneral);
          if (regs_it == in_regs.category_map().end()) {
            // TODO: coherent error message
            Cancel(err);
            return;
          }
          auto& regs = regs_it->second;
          // See if the stack pointer has regressed, if not, step some more.
          uint64_t stack_pointer =
              GetRegisterValue<uint64_t>(regs, debug_ipc::RegisterID::kX64_rsp);
          if (stack_pointer > first_sp_) {
            GetPerThreadState()[thread->GetKoid()] =
                PerThreadState::READING_ACTUAL_BYTES;
            GetResultAndContinue(thread);
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
      GetPerThreadState()[thread_koid_] = PerThreadState::READING_ACTUAL_BYTES;
      GetResultAndContinue(thread);
    });
    thread->Continue();
  }
}

void ZxChannelReadParamsBuilder::GetResultAndContinue(
    fxl::WeakPtr<zxdb::Thread> thread) {
  PerThreadState state = GetPerThreadState()[thread_koid_];

  // Read the filled in values for actual_bytes and actual_handles, then read
  // the memory at those locations, and then finish.
  if (state == PerThreadState::READING_ACTUAL_BYTES) {
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
            zxdb::Err e = zxdb::Err(zxdb::ErrType::kGeneral,
                                    "Malformed zx_channel_read call");
            Cancel(err);
            return;
          }
          GetResultAndContinue(thread);
        });
  } else if (state == PerThreadState::READING_ACTUAL_HANDLES) {
    GetMemoryAtAndThen<uint32_t>(
        thread, actual_handles_ptr_, sizeof(uint32_t),
        [this, thread](const zxdb::Err& err, std::unique_ptr<uint32_t> data) {
          if (!thread || !err.ok()) {
            Cancel(err);
            return;
          }
          num_handles_ = *data;
          GetPerThreadState()[thread_koid_] = PerThreadState::FILLING_IN_BYTES;
          GetResultAndContinue(thread);
        });
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
// the set of registers for that thread, and that both are on a connected x64
// device, do what is necessary to populate |params| and pass them to the
// callback.
//
// This remains pretty brittle WRT the order of parameters to zx_channel_read
// and the x86 calling conventions.  Those things aren't likely to change, but
// if they did, we'd have to update this.
void ZxChannelReadParamsBuilder::BuildX86AndContinue(
    fxl::WeakPtr<zxdb::Thread> thread, const std::vector<zxdb::Register>& regs,
    BreakpointRegisterer& registerer) {
  // The order of parameters in the System V AMD64 ABI we use, according to
  // Wikipedia:
  handle_ =
      GetRegisterValue<zx_handle_t>(regs, debug_ipc::RegisterID::kX64_rdi);
  options_ = GetRegisterValue<uint32_t>(regs, debug_ipc::RegisterID::kX64_rsi);
  bytes_address_ =
      GetRegisterValue<uint64_t>(regs, debug_ipc::RegisterID::kX64_rdx);
  handles_address_ =
      GetRegisterValue<uint64_t>(regs, debug_ipc::RegisterID::kX64_rcx);
  // The num_bytes and num_handles values are not useful, and are only included
  // here for documentation purposes.
  // num_bytes =
  //     GetRegisterValue<uint32_t>(regs, debug_ipc::RegisterID::kX64_r8);
  // num_handles =
  //     GetRegisterValue<uint32_t>(regs, debug_ipc::RegisterID::kX64_r9);

  // The remaining args are on the stack.  rsp + 8 is actual_bytes, and rsp
  // + 16 is actual_handles
  uint64_t stack_pointer =
      GetRegisterValue<uint64_t>(regs, debug_ipc::RegisterID::kX64_rsp);

  thread_koid_ = thread->GetKoid();
  registerer_ = &registerer;

  once_ = false;
  first_sp_ = stack_pointer;

  // Read the stack pointer and the appropriate offsets for &actual_bytes and
  // &actual_handles.
  // See the comment on ZxChannelParamsBuilder::BuildZxChannelParamsAndContinue
  // for detail on capturing |this|.
  GetMemoryAtAndThen<uint64_t[]>(
      thread, stack_pointer, 3 * sizeof(uint32_t*),
      [this, thread](const zxdb::Err& err, std::unique_ptr<uint64_t[]> data) {
        if (!thread || !err.ok()) {
          Cancel(err);
          return;
        }
        if (data == nullptr) {
          Cancel(zxdb::Err(zxdb::ErrType::kGeneral,
                           "Unable to read actual ptrs for zx_channel_read"));
          return;
        }
        // The remaining args are on the stack.  rsp + 8 is actual_bytes, and
        // rsp + 16 is actual_handles
        actual_bytes_ptr_ = *(reinterpret_cast<uint64_t*>(data.get() + 1));
        actual_handles_ptr_ = *(reinterpret_cast<uint64_t*>(data.get() + 2));
        GetPerThreadState()[thread_koid_] = PerThreadState::STEPPING;
        FinishChannelReadX86(thread);
      });
}

// Assuming that |thread| is stopped in a zx_channel_read, and that |regs| is
// the set of registers for that thread, and that both are on a connected ARM
// device, do what is necessary to populate |params| and pass them to the
// callback.
//
// This remains pretty brittle WRT the order of parameters to zx_channel_read
// and the ARM calling conventions.  Those things aren't likely to change, but
// if they did, we'd have to update this.
void ZxChannelReadParamsBuilder::BuildArmAndContinue(
    fxl::WeakPtr<zxdb::Thread> thread, const std::vector<zxdb::Register>& regs,
    BreakpointRegisterer& registerer) {
  // The order of parameters in the System V AMD64 ABI we use, according to
  // Wikipedia:
  handle_ =
      GetRegisterValue<zx_handle_t>(regs, debug_ipc::RegisterID::kARMv8_x0);
  options_ = GetRegisterValue<uint32_t>(regs, debug_ipc::RegisterID::kARMv8_x1);
  bytes_address_ =
      GetRegisterValue<uint64_t>(regs, debug_ipc::RegisterID::kARMv8_x2);
  handles_address_ =
      GetRegisterValue<uint64_t>(regs, debug_ipc::RegisterID::kARMv8_x3);
  // The num_bytes and num_handles values are not useful, and are only
  // included here for documentation purposes.
  // num_bytes =
  //     GetRegisterValue<uint32_t>(regs, debug_ipc::RegisterID::kARMv8_x4);
  // num_handles =
  //     GetRegisterValue<uint32_t>(regs, debug_ipc::RegisterID::kARMv8_x5);
  actual_bytes_ptr_ =
      GetRegisterValue<uint64_t>(regs, debug_ipc::RegisterID::kARMv8_x6);
  actual_handles_ptr_ =
      GetRegisterValue<uint64_t>(regs, debug_ipc::RegisterID::kARMv8_x7);

  uint64_t link_register =
      GetRegisterValue<uint64_t>(regs, debug_ipc::RegisterID::kARMv8_lr);

  first_sp_ =
      GetRegisterValue<uint64_t>(regs, debug_ipc::RegisterID::kARMv8_sp);

  thread_koid_ = thread->GetKoid();
  registerer_ = &registerer;

  once_ = false;
  GetPerThreadState()[thread_koid_] = PerThreadState::STEPPING;

  FinishChannelReadArm(thread, link_register);
}

}  // namespace fidlcat
