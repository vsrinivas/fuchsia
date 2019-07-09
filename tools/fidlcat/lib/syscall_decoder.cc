// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/syscall_decoder.h"

#include <zircon/system/public/zircon/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/register.h"
#include "src/developer/debug/zxdb/client/step_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/lib/fxl/logging.h"

// TODO: Look into this.  Removing the hack that led to this (in
// debug_ipc/helper/message_loop.h) seems to work, except it breaks SDK builds
// on CQ in a way I can't repro locally.
#undef __TA_REQUIRES

#include "tools/fidlcat/lib/interception_workflow.h"
#include "tools/fidlcat/lib/syscall_decoder.h"

namespace fidlcat {

#define ErrorNameCase(name) \
  case name:                \
    os << #name;            \
    return

// TODO: (use zx_status_get_string when it will be available).
void ErrorName(int64_t error_code, std::ostream& os) {
  switch (error_code) {
    ErrorNameCase(ZX_ERR_INTERNAL);
    ErrorNameCase(ZX_ERR_NOT_SUPPORTED);
    ErrorNameCase(ZX_ERR_NO_RESOURCES);
    ErrorNameCase(ZX_ERR_NO_MEMORY);
    ErrorNameCase(ZX_ERR_INTERNAL_INTR_RETRY);
    ErrorNameCase(ZX_ERR_INVALID_ARGS);
    ErrorNameCase(ZX_ERR_BAD_HANDLE);
    ErrorNameCase(ZX_ERR_WRONG_TYPE);
    ErrorNameCase(ZX_ERR_BAD_SYSCALL);
    ErrorNameCase(ZX_ERR_OUT_OF_RANGE);
    ErrorNameCase(ZX_ERR_BUFFER_TOO_SMALL);
    ErrorNameCase(ZX_ERR_BAD_STATE);
    ErrorNameCase(ZX_ERR_TIMED_OUT);
    ErrorNameCase(ZX_ERR_SHOULD_WAIT);
    ErrorNameCase(ZX_ERR_CANCELED);
    ErrorNameCase(ZX_ERR_PEER_CLOSED);
    ErrorNameCase(ZX_ERR_NOT_FOUND);
    ErrorNameCase(ZX_ERR_ALREADY_EXISTS);
    ErrorNameCase(ZX_ERR_ALREADY_BOUND);
    ErrorNameCase(ZX_ERR_UNAVAILABLE);
    ErrorNameCase(ZX_ERR_ACCESS_DENIED);
    ErrorNameCase(ZX_ERR_IO);
    ErrorNameCase(ZX_ERR_IO_REFUSED);
    ErrorNameCase(ZX_ERR_IO_DATA_INTEGRITY);
    ErrorNameCase(ZX_ERR_IO_DATA_LOSS);
    ErrorNameCase(ZX_ERR_IO_NOT_PRESENT);
    ErrorNameCase(ZX_ERR_IO_OVERRUN);
    ErrorNameCase(ZX_ERR_IO_MISSED_DEADLINE);
    ErrorNameCase(ZX_ERR_IO_INVALID);
    ErrorNameCase(ZX_ERR_BAD_PATH);
    ErrorNameCase(ZX_ERR_NOT_DIR);
    ErrorNameCase(ZX_ERR_NOT_FILE);
    ErrorNameCase(ZX_ERR_FILE_BIG);
    ErrorNameCase(ZX_ERR_NO_SPACE);
    ErrorNameCase(ZX_ERR_NOT_EMPTY);
    ErrorNameCase(ZX_ERR_STOP);
    ErrorNameCase(ZX_ERR_NEXT);
    ErrorNameCase(ZX_ERR_ASYNC);
    ErrorNameCase(ZX_ERR_PROTOCOL_NOT_SUPPORTED);
    ErrorNameCase(ZX_ERR_ADDRESS_UNREACHABLE);
    ErrorNameCase(ZX_ERR_ADDRESS_IN_USE);
    ErrorNameCase(ZX_ERR_NOT_CONNECTED);
    ErrorNameCase(ZX_ERR_CONNECTION_REFUSED);
    ErrorNameCase(ZX_ERR_CONNECTION_RESET);
    ErrorNameCase(ZX_ERR_CONNECTION_ABORTED);
    default:
      os << "errno=" << error_code;
      return;
  }
}

// Helper function to convert a vector of bytes to a T.
template <typename T>
T GetValueFromBytes(const std::vector<uint8_t>& bytes, size_t offset) {
  T ret = 0;
  for (size_t i = 0; (i < sizeof(ret)) && (offset < bytes.size()); i++) {
    ret |= ((uint64_t)(bytes[offset++])) << (i * 8);
  }
  return ret;
}

uint64_t GetRegisterValue(const std::vector<zxdb::Register>& general_registers,
                          const debug_ipc::RegisterID register_id) {
  for (const auto& reg : general_registers) {
    if (reg.id() == register_id) {
      return GetValueFromBytes<uint64_t>(reg.data(), 0);
    }
  }
  return 0;
}

void MemoryDumpToVector(const zxdb::MemoryDump& dump, std::vector<uint8_t>* output_vector) {
  output_vector->reserve(dump.size());
  for (const debug_ipc::MemoryBlock& block : dump.blocks()) {
    FXL_DCHECK(block.valid);
    for (size_t offset = 0; offset < block.size; ++offset) {
      output_vector->push_back(block.data[offset]);
    }
  }
}

void SyscallUse::SyscallInputsDecoded(SyscallDecoder* syscall) {}

void SyscallUse::SyscallOutputsDecoded(SyscallDecoder* syscall) { syscall->Destroy(); }

void SyscallUse::SyscallDecodingError(const SyscallDecoderError& error, SyscallDecoder* syscall) {
  FXL_LOG(ERROR) << error.message();
  syscall->Destroy();
}

void SyscallDecoder::LoadMemory(uint64_t address, size_t size, std::vector<uint8_t>* destination) {
  if (address == 0) {
    // Null pointer => don't load anything.
    return;
  }
  ++pending_request_count_;
  thread_->GetProcess()->ReadMemory(
      address, size,
      [this, address, size, destination](const zxdb::Err& err, zxdb::MemoryDump dump) {
        --pending_request_count_;
        if (!err.ok()) {
          Error(SyscallDecoderError::Type::kCantReadMemory)
              << "Can't load memory at " << address << ": " << err.msg();
        } else if ((dump.size() != size) || !dump.AllValid()) {
          Error(SyscallDecoderError::Type::kCantReadMemory)
              << "Can't load memory at " << address << ": not enough data";
        } else {
          MemoryDumpToVector(dump, destination);
        }
        if (input_arguments_loaded_) {
          LoadOutputs();
        } else {
          LoadInputs();
        }
      });
}

void SyscallDecoder::LoadArgument(int argument_index, size_t size) {
  if (decoded_arguments_[argument_index].loading()) {
    return;
  }
  decoded_arguments_[argument_index].set_loading();
  LoadMemory(Value(argument_index), size, &decoded_arguments_[argument_index].loaded_values());
}

void SyscallDecoder::Decode() {
  static std::vector<debug_ipc::RegisterCategory::Type> types = {
      debug_ipc::RegisterCategory::Type::kGeneral};
  const std::vector<zxdb::Register>& general_registers =
      thread_->GetStack()[0]->GetGeneralRegisters();

  // The order of parameters in the System V AMD64 ABI we use, according to
  // Wikipedia:
  static debug_ipc::RegisterID amd64_abi[] = {
      debug_ipc::RegisterID::kX64_rdi, debug_ipc::RegisterID::kX64_rsi,
      debug_ipc::RegisterID::kX64_rdx, debug_ipc::RegisterID::kX64_rcx,
      debug_ipc::RegisterID::kX64_r8,  debug_ipc::RegisterID::kX64_r9};

  // The order of parameters in the System V AArch64 ABI we use, according to
  // Wikipedia:
  static debug_ipc::RegisterID aarch64_abi[] = {
      debug_ipc::RegisterID::kARMv8_x0, debug_ipc::RegisterID::kARMv8_x1,
      debug_ipc::RegisterID::kARMv8_x2, debug_ipc::RegisterID::kARMv8_x3,
      debug_ipc::RegisterID::kARMv8_x4, debug_ipc::RegisterID::kARMv8_x5,
      debug_ipc::RegisterID::kARMv8_x6, debug_ipc::RegisterID::kARMv8_x7};

  debug_ipc::RegisterID* abi;
  size_t register_count;
  if (arch_ == debug_ipc::Arch::kX64) {
    abi = amd64_abi;
    register_count = sizeof(amd64_abi) / sizeof(debug_ipc::RegisterID);
    entry_sp_ = GetRegisterValue(general_registers, debug_ipc::RegisterID::kX64_rsp);
  } else if (arch_ == debug_ipc::Arch::kArm64) {
    abi = aarch64_abi;
    register_count = sizeof(aarch64_abi) / sizeof(debug_ipc::RegisterID);
    entry_sp_ = GetRegisterValue(general_registers, debug_ipc::RegisterID::kARMv8_sp);
    return_address_ = GetRegisterValue(general_registers, debug_ipc::RegisterID::kARMv8_lr);
  } else {
    Error(SyscallDecoderError::Type::kUnknownArchitecture) << "Unknown architecture";
    use_->SyscallDecodingError(error_, this);
    return;
  }

  size_t argument_count = syscall_->arguments().size();
  decoded_arguments_.reserve(argument_count);
  register_count = std::min(argument_count, register_count);
  for (size_t i = 0; i < register_count; i++) {
    decoded_arguments_.emplace_back(GetRegisterValue(general_registers, abi[i]));
  }

  LoadStack();
}

void SyscallDecoder::LoadStack() {
  size_t stack_size = (syscall_->arguments().size() - decoded_arguments_.size()) * sizeof(uint64_t);
  if (arch_ == debug_ipc::Arch::kX64) {
    stack_size += sizeof(uint64_t);
  }
  if (stack_size == 0) {
    LoadInputs();
    return;
  }
  uint64_t address = entry_sp_;
  ++pending_request_count_;
  thread_->GetProcess()->ReadMemory(
      address, stack_size,
      [this, address, stack_size](const zxdb::Err& err, zxdb::MemoryDump dump) {
        --pending_request_count_;
        if (!err.ok()) {
          Error(SyscallDecoderError::Type::kCantReadMemory)
              << "Can't load stack at " << address << ": " << err.msg();
        } else if ((dump.size() != stack_size) || !dump.AllValid()) {
          Error(SyscallDecoderError::Type::kCantReadMemory)
              << "Can't load stack at " << address << ": not enough data";
        } else {
          std::vector<uint8_t> data;
          MemoryDumpToVector(dump, &data);
          size_t offset = 0;
          if (arch_ == debug_ipc::Arch::kX64) {
            return_address_ = GetValueFromBytes<uint64_t>(data, 0);
            offset += sizeof(uint64_t);
          }
          while (offset < data.size()) {
            decoded_arguments_.emplace_back(GetValueFromBytes<uint64_t>(data, offset));
            offset += sizeof(uint64_t);
          }
        }
        LoadInputs();
      });
}

void SyscallDecoder::LoadInputs() {
  if (error_.type() != SyscallDecoderError::Type::kNone) {
    if (pending_request_count_ == 0) {
      use_->SyscallDecodingError(error_, this);
    }
    return;
  }
  for (const auto& input : syscall_->inputs()) {
    input->Load(this);
  }
  if (pending_request_count_ > 0) {
    return;
  }
  input_arguments_loaded_ = true;
  if (error_.type() != SyscallDecoderError::Type::kNone) {
    use_->SyscallDecodingError(error_, this);
  } else {
    StepToReturnAddress();
  }
}

void SyscallDecoder::StepToReturnAddress() {
  use_->SyscallInputsDecoded(this);
  zxdb::BreakpointSettings settings;
  settings.enabled = true;
  settings.stop_mode = zxdb::BreakpointSettings::StopMode::kThread;
  settings.type = debug_ipc::BreakpointType::kSoftware;
  settings.location.address = return_address_;
  settings.location.type = zxdb::InputLocation::Type::kAddress;
  settings.scope = zxdb::BreakpointSettings::Scope::kThread;
  settings.scope_thread = thread_.get();
  settings.scope_target = thread_->GetProcess()->GetTarget();
  settings.one_shot = true;
  thread_observer_->CreateNewBreakpoint(settings);
  // Registers a one time breakpoint for this decoder.
  thread_observer_->Register(thread_->GetKoid(), this);
  // Restarts the stopped thread. When the breakpoint will be reached (at the
  // end of the syscall), LoadSyscallReturnValue will be called.
  thread_->Continue();
}

void SyscallDecoder::LoadSyscallReturnValue() {
  const std::vector<zxdb::Register>& general_registers =
      thread_->GetStack()[0]->GetGeneralRegisters();

  debug_ipc::RegisterID result_register = (arch_ == debug_ipc::Arch::kX64)
                                              ? debug_ipc::RegisterID::kX64_rax
                                              : debug_ipc::RegisterID::kARMv8_x0;
  syscall_return_value_ = GetRegisterValue(general_registers, result_register);

  LoadOutputs();
}

void SyscallDecoder::LoadOutputs() {
  if (error_.type() != SyscallDecoderError::Type::kNone) {
    if (pending_request_count_ == 0) {
      use_->SyscallDecodingError(error_, this);
    }
    return;
  }
  for (const auto& output : syscall_->outputs()) {
    if (output->error_code() == static_cast<zx_status_t>(syscall_return_value_)) {
      output->Load(this);
    }
  }
  if (pending_request_count_ > 0) {
    return;
  }
  if (error_.type() != SyscallDecoderError::Type::kNone) {
    use_->SyscallDecodingError(error_, this);
  } else {
    DecodeAndDisplay();
  }
}

void SyscallDecoder::DecodeAndDisplay() {
  if (pending_request_count_ > 0) {
    return;
  }
  use_->SyscallOutputsDecoded(this);
}

void SyscallDecoder::Destroy() { dispatcher_->DeleteDecoder(this); }

void SyscallDisplay::SyscallInputsDecoded(SyscallDecoder* syscall) {
  const Colors& colors = dispatcher_->colors();
  line_header_ = syscall->thread()->GetProcess()->GetName() + ' ' + colors.red +
                 std::to_string(syscall->thread()->GetProcess()->GetKoid()) + colors.reset + ':' +
                 colors.red + std::to_string(syscall->thread_id()) + colors.reset + ' ';

  if (dispatcher_->with_process_info()) {
    os_ << line_header_ << '\n';
  } else {
    os_ << '\n';
  }

  // Displays the header and the inline input arguments.
  os_ << line_header_ << syscall->syscall()->name() << '(';
  const char* separator = "";
  for (const auto& input : syscall->syscall()->inputs()) {
    separator = input->DisplayInline(dispatcher_, syscall, separator, os_);
  }
  os_ << ")\n";

  if (!dispatcher_->with_process_info()) {
    line_header_ = "";
  }

  // Displays the outline input arguments.
  for (const auto& input : syscall->syscall()->inputs()) {
    input->DisplayOutline(dispatcher_, syscall, line_header_, /*tabs=*/1, os_);
  }
  dispatcher_->set_last_displayed_syscall(this);
}

void SyscallDisplay::SyscallOutputsDecoded(SyscallDecoder* syscall) {
  if (dispatcher_->last_displayed_syscall() != this) {
    // Add a blank line to tell the user that this display is not linked to the
    // previous displayed lines.
    os_ << "\n";
  }
  const Colors& colors = dispatcher_->colors();
  // Displays the returned value.
  os_ << line_header_ << "  -> ";
  if (static_cast<zx_status_t>(syscall->syscall_return_value()) == ZX_OK) {
    os_ << colors.green << "ZX_OK" << colors.reset;
  } else {
    os_ << colors.red;
    ErrorName(static_cast<zx_status_t>(syscall->syscall_return_value()), os_);
    os_ << colors.reset;
  }
  // And the inline output arguments (if any).
  const char* separator = " (";
  for (const auto& output : syscall->syscall()->outputs()) {
    if (output->error_code() == static_cast<zx_status_t>(syscall->syscall_return_value())) {
      separator = output->DisplayInline(dispatcher_, syscall, separator, os_);
    }
  }
  if (std::string(" (") != separator) {
    os_ << ')';
  }
  os_ << '\n';
  // Displays the outline output arguments.
  for (const auto& output : syscall->syscall()->outputs()) {
    if (output->error_code() == static_cast<zx_status_t>(syscall->syscall_return_value())) {
      output->DisplayOutline(dispatcher_, syscall, line_header_, /*tabs=*/2, os_);
    }
  }
  // Now our job is done, we can destroy the object.
  syscall->Destroy();
}

void SyscallDisplay::SyscallDecodingError(const SyscallDecoderError& error,
                                          SyscallDecoder* syscall) {
  std::string message = error.message();
  size_t pos = 0;
  for (;;) {
    size_t end = message.find('\n', pos);
    const Colors& colors = dispatcher_->colors();
    os_ << syscall->thread()->GetProcess()->GetName() << ' ' << colors.red
        << syscall->thread()->GetProcess()->GetKoid() << colors.reset << ':' << colors.red
        << syscall->thread_id() << colors.reset << ' ' << syscall->syscall()->name() << ": "
        << colors.red << error.message().substr(pos, end) << colors.reset << '\n';
    if (end == std::string::npos) {
      break;
    }
    pos = end + 1;
  }
  os_ << '\n';
  syscall->Destroy();
}

}  // namespace fidlcat
