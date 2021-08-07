// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/syscall_decoder.h"

#include <lib/syslog/cpp/macros.h>
#include <sys/time.h>
#include <zircon/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/step_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"
#include "tools/fidlcat/lib/interception_workflow.h"
#include "tools/fidlcat/lib/type_decoder.h"

namespace fidlcat {

constexpr int kBitsPerByte = 8;

// Helper function to convert a vector of bytes to a T.
template <typename T>
T GetValueFromBytes(const std::vector<uint8_t>& bytes, size_t offset) {
  T ret = 0;
  for (size_t i = 0; (i < sizeof(ret)) && (offset < bytes.size()); i++) {
    ret |= static_cast<uint64_t>(bytes[offset++]) << (i * kBitsPerByte);
  }
  return ret;
}

uint64_t GetRegisterValue(const std::vector<debug_ipc::Register>& general_registers,
                          const debug::RegisterID register_id) {
  for (const auto& reg : general_registers) {
    if (reg.id == register_id) {
      return GetValueFromBytes<uint64_t>(reg.data, 0);
    }
  }
  return 0;
}

void MemoryDumpToVector(const zxdb::MemoryDump& dump, std::vector<uint8_t>* output_vector) {
  output_vector->reserve(dump.size());
  for (const debug_ipc::MemoryBlock& block : dump.blocks()) {
    FX_DCHECK(block.valid);
    for (size_t offset = 0; offset < block.size; ++offset) {
      output_vector->push_back(block.data[offset]);
    }
  }
}

SyscallDecoder::SyscallDecoder(SyscallDecoderDispatcher* dispatcher,
                               InterceptingThreadObserver* thread_observer, zxdb::Thread* thread,
                               const Syscall* syscall, int64_t timestamp)
    : SyscallDecoderInterface(dispatcher, thread),
      thread_observer_(thread_observer),
      weak_thread_(thread->GetWeakPtr()),
      syscall_(syscall),
      timestamp_(timestamp) {
  if (fidlcat_thread_ == nullptr) {
    Process* fidlcat_process = dispatcher_->SearchProcess(thread->GetProcess()->GetKoid());
    if (fidlcat_process == nullptr) {
      fidlcat_process = dispatcher_->CreateProcess(thread->GetProcess()->GetName(),
                                                   thread->GetProcess()->GetKoid(),
                                                   thread->GetProcess()->GetWeakPtr());
    }
    fidlcat_thread_ = dispatcher_->CreateThread(thread->GetKoid(), fidlcat_process);
  }
}

void SyscallDecoder::SyscallDecodingError() {
  dispatcher_->SyscallDecodingError(fidlcat_thread_, syscall_, error_);
  Destroy();
}

void SyscallDecoder::LoadMemory(uint64_t address, size_t size, std::vector<uint8_t>* destination) {
  if (address == 0) {
    // Null pointer => don't load anything.
    return;
  }
  zxdb::Thread* thread = get_thread();
  if (thread == nullptr) {
    aborted_ = true;
    Destroy();
  }
  ++pending_request_count_;
  thread->GetProcess()->ReadMemory(
      address, size,
      [this, address, size, destination](const zxdb::Err& err, zxdb::MemoryDump dump) {
        --pending_request_count_;
        if (aborted()) {
          Destroy();
        } else {
          if (!err.ok()) {
            Error(DecoderError::Type::kCantReadMemory)
                << "Can't load memory at " << address << ": " << err.msg();
          } else if ((dump.size() != size) || !dump.AllValid()) {
            Error(DecoderError::Type::kCantReadMemory)
                << "Can't load memory at " << address << ": not enough data";
          } else {
            MemoryDumpToVector(dump, destination);
          }
          if (input_arguments_loaded_) {
            LoadOutputs();
          } else {
            LoadInputs();
          }
        }
      });
}

void SyscallDecoder::LoadArgument(Stage stage, int argument_index, size_t size) {
  if (decoded_arguments_[argument_index].loading(stage)) {
    return;
  }
  decoded_arguments_[argument_index].set_loading(stage);
  LoadMemory(ArgumentValue(argument_index), size,
             &decoded_arguments_[argument_index].loaded_values(stage));
}

void SyscallDecoder::LoadBuffer(Stage stage, uint64_t address, size_t size) {
  if (address == 0) {
    return;
  }
  SyscallDecoderBuffer& buffer = buffers_[std::make_pair(stage, address)];
  if (buffer.loading()) {
    return;
  }
  buffer.set_loading();
  LoadMemory(address, size, &buffer.loaded_values());
}

void SyscallDecoder::Decode() {
  zxdb::Thread* thread = weak_thread_.get();
  if (aborted_ || (thread == nullptr) || (thread->GetStack().size() == 0)) {
    aborted_ = true;
    Destroy();
    return;
  }
  if (dispatcher_->decode_options().stack_level >= kFullStack) {
    thread->GetStack().SyncFrames([this](const zxdb::Err& /*err*/) { DoDecode(); });
  } else {
    DoDecode();
  }
}

void SyscallDecoder::DoDecode() {
  zxdb::Thread* thread = weak_thread_.get();
  if (aborted_ || (thread == nullptr) || (thread->GetStack().size() == 0)) {
    aborted_ = true;
    Destroy();
    return;
  }
  const zxdb::Stack& stack = thread->GetStack();
  // Don't keep the inner frame which is the syscall and is not useful.
  for (size_t i = stack.size() - 1; i > 0; --i) {
    const zxdb::Frame* caller = stack[i];
    caller_locations_.push_back(caller->GetLocation());
  }
  const std::vector<debug_ipc::Register>* general_registers =
      thread->GetStack()[0]->GetRegisterCategorySync(debug_ipc::RegisterCategory::kGeneral);
  FX_DCHECK(general_registers);  // General registers should always be available synchronously.

  // The order of parameters in the System V AMD64 ABI we use, according to
  // Wikipedia:
  static std::vector<debug::RegisterID> amd64_abi = {
      debug::RegisterID::kX64_rdi, debug::RegisterID::kX64_rsi, debug::RegisterID::kX64_rdx,
      debug::RegisterID::kX64_rcx, debug::RegisterID::kX64_r8,  debug::RegisterID::kX64_r9};

  // The order of parameters in the System V AArch64 ABI we use, according to
  // Wikipedia:
  static std::vector<debug::RegisterID> aarch64_abi = {
      debug::RegisterID::kARMv8_x0, debug::RegisterID::kARMv8_x1, debug::RegisterID::kARMv8_x2,
      debug::RegisterID::kARMv8_x3, debug::RegisterID::kARMv8_x4, debug::RegisterID::kARMv8_x5,
      debug::RegisterID::kARMv8_x6, debug::RegisterID::kARMv8_x7};

  const std::vector<debug::RegisterID>* abi;
  if (arch_ == debug::Arch::kX64) {
    abi = &amd64_abi;
    entry_sp_ = GetRegisterValue(*general_registers, debug::RegisterID::kX64_rsp);
  } else if (arch_ == debug::Arch::kArm64) {
    abi = &aarch64_abi;
    entry_sp_ = GetRegisterValue(*general_registers, debug::RegisterID::kARMv8_sp);
    return_address_ = GetRegisterValue(*general_registers, debug::RegisterID::kARMv8_lr);
  } else {
    Error(DecoderError::Type::kUnknownArchitecture) << "Unknown architecture";
    if (pending_request_count_ == 0) {
      SyscallDecodingError();
    }
    return;
  }

  size_t argument_count = syscall_->arguments().size();
  decoded_arguments_.reserve(argument_count);
  size_t register_count = std::min(argument_count, abi->size());
  for (size_t i = 0; i < register_count; i++) {
    decoded_arguments_.emplace_back(GetRegisterValue(*general_registers, (*abi)[i]));
  }

  LoadStack();
}

void SyscallDecoder::LoadStack() {
  zxdb::Thread* thread = weak_thread_.get();
  if (aborted_ || (thread == nullptr) || (thread->GetStack().size() == 0)) {
    aborted_ = true;
    Destroy();
    return;
  }
  size_t stack_size = (syscall_->arguments().size() - decoded_arguments_.size()) * sizeof(uint64_t);
  if (arch_ == debug::Arch::kX64) {
    stack_size += sizeof(uint64_t);
  }
  if (stack_size == 0) {
    LoadInputs();
    return;
  }
  uint64_t address = entry_sp_;
  ++pending_request_count_;
  thread->GetProcess()->ReadMemory(
      address, stack_size,
      [this, address, stack_size](const zxdb::Err& err, zxdb::MemoryDump dump) {
        --pending_request_count_;
        if (aborted()) {
          Destroy();
        } else {
          if (!err.ok()) {
            Error(DecoderError::Type::kCantReadMemory)
                << "Can't load stack at " << address << '/' << stack_size << ": " << err.msg();
          } else if ((dump.size() != stack_size) || !dump.AllValid()) {
            Error(DecoderError::Type::kCantReadMemory)
                << "Can't load stack at " << address << '/' << stack_size << ": not enough data";
          } else {
            std::vector<uint8_t> data;
            MemoryDumpToVector(dump, &data);
            size_t offset = 0;
            if (arch_ == debug::Arch::kX64) {
              return_address_ = GetValueFromBytes<uint64_t>(data, 0);
              offset += sizeof(uint64_t);
            }
            while (offset < data.size()) {
              decoded_arguments_.emplace_back(GetValueFromBytes<uint64_t>(data, offset));
              offset += sizeof(uint64_t);
            }
          }
          LoadInputs();
        }
      });
}

void SyscallDecoder::LoadInputs() {
  if (error_.type() != DecoderError::Type::kNone) {
    if (pending_request_count_ == 0) {
      SyscallDecodingError();
    }
    return;
  }
  for (const auto& input : syscall_->inputs()) {
    if (input->ConditionsAreTrue(this, Stage::kEntry)) {
      input->Load(this, Stage::kEntry);
    }
  }
  if (pending_request_count_ > 0) {
    return;
  }
  input_arguments_loaded_ = true;
  if (error_.type() != DecoderError::Type::kNone) {
    SyscallDecodingError();
  } else {
    if (StepToReturnAddress()) {
      DecodeInputs();
    }
  }
}

bool SyscallDecoder::StepToReturnAddress() {
  zxdb::Thread* thread = weak_thread_.get();
  if (aborted_ || (thread == nullptr) || (thread->GetStack().size() == 0)) {
    aborted_ = true;
    Destroy();
    return false;
  }

  if (syscall_->return_type() != SyscallReturnType::kNoReturn) {
    thread_observer_->Register(fidlcat_thread()->koid(), this);
    thread_observer_->AddExitBreakpoint(thread, *syscall_, return_address_);
  }

  // Restarts the stopped thread. When the breakpoint will be reached (at the
  // end of the syscall), LoadSyscallReturnValue will be called.
  thread->Continue(false);
  return true;
}

void SyscallDecoder::DecodeInputs() {
  // Creates the invoked event.
  invoked_event_ = std::make_shared<InvokedEvent>(timestamp(), fidlcat_thread_, syscall_);
  auto inline_member = syscall_->input_inline_members().begin();
  auto outline_member = syscall_->input_outline_members().begin();
  for (const auto& input : syscall_->inputs()) {
    if (input->InlineValue()) {
      if (input->ConditionsAreTrue(this, Stage::kEntry)) {
        FX_DCHECK(inline_member != syscall_->input_inline_members().end());
        std::unique_ptr<fidl_codec::Value> value = input->GenerateValue(this, Stage::kEntry);
        FX_DCHECK(value != nullptr);
        invoked_event_->AddInlineField(inline_member->get(), std::move(value));
      }
      ++inline_member;
    } else {
      if (input->ConditionsAreTrue(this, Stage::kEntry)) {
        FX_DCHECK(outline_member != syscall_->input_outline_members().end());
        std::unique_ptr<fidl_codec::Value> value = input->GenerateValue(this, Stage::kEntry);
        FX_DCHECK(value != nullptr);
        invoked_event_->AddOutlineField(outline_member->get(), std::move(value));
      }
      ++outline_member;
    }
  }
  if (dispatcher_->needs_stack_frame()) {
    CopyStackFrame(caller_locations(), &invoked_event_->stack_frame());
  }
  if (invoked_event_->NeedsToLoadHandleInfo(&dispatcher_->inference())) {
    fidlcat_thread_->process()->LoadHandleInfo(&dispatcher_->inference());
  }
  // Eventually calls the code before displaying the input (which may invalidate
  // the display).
  if ((syscall_->inputs_decoded_action() == nullptr) ||
      (dispatcher_->*(syscall_->inputs_decoded_action()))(timestamp(), this)) {
    dispatcher_->AddInvokedEvent(invoked_event_);
  }

  if (syscall_->return_type() == SyscallReturnType::kNoReturn) {
    // We already called Continue in StepToReturnAddress. We don't want to call it twice. We set
    // aborted_ to avoid that.
    aborted_ = true;
    // We don't expect the syscall to return and it doesn't have any output. We can now destroy
    // the decoder.
    Destroy();
  }
}

void SyscallDecoder::LoadSyscallReturnValue() {
  zxdb::Thread* thread = weak_thread_.get();
  if (aborted_ || (thread == nullptr) || (thread->GetStack().size() == 0)) {
    aborted_ = true;
    Destroy();
    return;
  }
  const std::vector<debug_ipc::Register>* general_registers =
      thread->GetStack()[0]->GetRegisterCategorySync(debug_ipc::RegisterCategory::kGeneral);
  FX_DCHECK(general_registers);  // General registers should always be available synchronously.

  debug::RegisterID result_register =
      (arch_ == debug::Arch::kX64) ? debug::RegisterID::kX64_rax : debug::RegisterID::kARMv8_x0;
  syscall_return_value_ = GetRegisterValue(*general_registers, result_register);

  LoadOutputs();
}

void SyscallDecoder::LoadOutputs() {
  if (error_.type() != DecoderError::Type::kNone) {
    if (pending_request_count_ == 0) {
      SyscallDecodingError();
    }
    return;
  }
  for (const auto& output : syscall_->outputs()) {
    if ((output->error_code() == static_cast<zx_status_t>(syscall_return_value_)) &&
        output->ConditionsAreTrue(this, Stage::kExit)) {
      output->Load(this, Stage::kExit);
    }
  }
  if (pending_request_count_ > 0) {
    return;
  }
  if (error_.type() != DecoderError::Type::kNone) {
    SyscallDecodingError();
  } else {
    DecodeOutputs();
  }
}

void SyscallDecoder::DecodeOutputs() {
  if (pending_request_count_ > 0) {
    return;
  }
  // Creates the output event.
  output_event_ = std::make_shared<OutputEvent>(timestamp(), fidlcat_thread_, syscall_,
                                                syscall_return_value_, invoked_event_);
  auto inline_member = syscall_->output_inline_members().begin();
  auto outline_member = syscall_->output_outline_members().begin();
  for (const auto& output : syscall_->outputs()) {
    if (output->InlineValue()) {
      if ((output->error_code() == static_cast<zx_status_t>(syscall_return_value_)) &&
          (output->ConditionsAreTrue(this, Stage::kExit))) {
        FX_DCHECK(inline_member != syscall_->output_inline_members().end());
        std::unique_ptr<fidl_codec::Value> value = output->GenerateValue(this, Stage::kExit);
        FX_DCHECK(value != nullptr);
        output_event_->AddInlineField(inline_member->get(), std::move(value));
      }
      ++inline_member;
    } else {
      if ((output->error_code() == static_cast<zx_status_t>(syscall_return_value_)) &&
          (output->ConditionsAreTrue(this, Stage::kExit))) {
        FX_DCHECK(outline_member != syscall_->output_outline_members().end());
        std::unique_ptr<fidl_codec::Value> value = output->GenerateValue(this, Stage::kExit);
        FX_DCHECK(value != nullptr);
        output_event_->AddOutlineField(outline_member->get(), std::move(value));
      }
      ++outline_member;
    }
  }
  if (output_event_->NeedsToLoadHandleInfo(&dispatcher_->inference())) {
    fidlcat_thread_->process()->LoadHandleInfo(&dispatcher_->inference());
  }
  if (syscall_->inference() != nullptr) {
    // Executes the inference associated with the syscall.
    // This is used to infer semantic about handles.
    (dispatcher_->*(syscall_->inference()))(output_event_.get(), semantic());
  }

  // If we have been able to generate an invoked event, directly call the dispatcher.
  dispatcher_->AddOutputEvent(output_event_);

  // Now our job is done, we can destroy the object.
  Destroy();
}

void SyscallDecoder::Destroy() {
  if (pending_request_count_ == 0) {
    dispatcher_->DeleteDecoder(this);
  }
}

}  // namespace fidlcat
