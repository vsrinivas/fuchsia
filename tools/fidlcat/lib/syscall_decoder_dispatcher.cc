// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

#include <zircon/system/public/zircon/errors.h>
#include <zircon/system/public/zircon/types.h>

#include <cstdint>
#include <memory>
#include <sstream>

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "tools/fidlcat/lib/inference.h"
#include "tools/fidlcat/lib/syscall_decoder.h"

namespace fidlcat {

std::unique_ptr<fidl_codec::Type> SyscallTypeToFidlCodecType(fidlcat::SyscallType syscall_type) {
  switch (syscall_type) {
    case SyscallType::kBool:
      return std::make_unique<fidl_codec::BoolType>();
    case SyscallType::kInt32:
      return std::make_unique<fidl_codec::Int32Type>();
    case SyscallType::kInt64:
      return std::make_unique<fidl_codec::Int64Type>();
    case SyscallType::kUint8:
      return std::make_unique<fidl_codec::Uint8Type>();
    case SyscallType::kUint8Hexa:
      return std::make_unique<fidl_codec::Uint8Type>(fidl_codec::Uint8Type::Kind::kHexaDecimal);
    case SyscallType::kUint16:
      return std::make_unique<fidl_codec::Uint16Type>();
    case SyscallType::kUint16Hexa:
      return std::make_unique<fidl_codec::Uint16Type>(fidl_codec::Uint16Type::Kind::kHexaDecimal);
    case SyscallType::kUint32:
      return std::make_unique<fidl_codec::Uint32Type>();
    case SyscallType::kUint32Hexa:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kHexaDecimal);
    case SyscallType::kUint64:
      return std::make_unique<fidl_codec::Uint64Type>();
    case SyscallType::kUint64Hexa:
      return std::make_unique<fidl_codec::Uint64Type>(fidl_codec::Uint64Type::Kind::kHexaDecimal);
    case SyscallType::kVaddr:
      return std::make_unique<fidl_codec::Uint64Type>(fidl_codec::Uint64Type::Kind::kVaddr);
    case SyscallType::kSize:
      return std::make_unique<fidl_codec::Uint64Type>(fidl_codec::Uint64Type::Kind::kSize);
    case SyscallType::kHandle:
      return std::make_unique<fidl_codec::HandleType>();
    case SyscallType::kBtiPerm:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kBtiPerm);
    case SyscallType::kRights:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kRights);
    case SyscallType::kExceptionState:
      return std::make_unique<fidl_codec::Uint32Type>(
          fidl_codec::Uint32Type::Kind::kExceptionState);
    case SyscallType::kPropType:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kPropType);
    case SyscallType::kCachePolicy:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kCachePolicy);
    case SyscallType::kClock:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kClock);
    case SyscallType::kDuration:
      return std::make_unique<fidl_codec::Int64Type>(fidl_codec::Int64Type::Kind::kDuration);
    case SyscallType::kTime:
      return std::make_unique<fidl_codec::Int64Type>(fidl_codec::Int64Type::Kind::kTime);
    default:
      return nullptr;
  }
}

std::unique_ptr<fidl_codec::Type> AccessBase::ComputeType() const {
  return SyscallTypeToFidlCodecType(GetSyscallType());
}

std::unique_ptr<fidl_codec::Type> SyscallInputOutputBase::ComputeType() const { return nullptr; }

std::unique_ptr<fidl_codec::Value> SyscallInputOutputBase::GenerateValue(SyscallDecoder* decoder,
                                                                         Stage stage) const {
  return std::make_unique<fidl_codec::InvalidValue>();
}

void SyscallInputOutputStringBuffer::DisplayOutline(SyscallDecoder* decoder, Stage stage,
                                                    fidl_codec::PrettyPrinter& printer) const {
  printer << name();
  printer << ':' << fidl_codec::Green << "string" << fidl_codec::ResetColor << ": ";
  const char* const* buffer = buffer_->Content(decoder, stage);
  if (buffer == nullptr) {
    printer << fidl_codec::Red << "nullptr" << fidl_codec::ResetColor;
  } else {
    uint32_t count = count_->Value(decoder, stage);
    if (count == 0) {
      printer << "empty\n";
      return;
    }
    const char* separator = "";
    for (uint32_t i = 0; i < count; ++i) {
      if (buffer[i] != nullptr) {
        printer << separator;
        const char* string = reinterpret_cast<const char*>(
            decoder->BufferContent(stage, reinterpret_cast<uint64_t>(buffer[i])));
        size_t string_size = (string == nullptr) ? 0 : strnlen(string, max_size_);
        printer.DisplayString(std::string_view(string, string_size));
        separator = ", ";
      }
    }
  }
  printer << '\n';
}

const char* SyscallInputOutputFixedSizeString::DisplayInline(
    SyscallDecoder* decoder, Stage stage, const char* separator,
    fidl_codec::PrettyPrinter& printer) const {
  printer << separator;
  printer << name() << ':' << fidl_codec::Green << "string" << fidl_codec::ResetColor << ": ";
  const char* string = string_->Content(decoder, stage);
  size_t string_size = (string == nullptr) ? 0 : strnlen(string, string_size_);
  printer.DisplayString(std::string_view(string, string_size));
  return ", ";
}

std::unique_ptr<fidl_codec::Type> SyscallFidlMessageHandle::ComputeType() const {
  return std::make_unique<fidl_codec::FidlMessageType>();
}

std::unique_ptr<fidl_codec::Value> SyscallFidlMessageHandle::GenerateValue(SyscallDecoder* decoder,
                                                                           Stage stage) const {
  zx_handle_t handle_value = handle()->Value(decoder, stage);
  const uint8_t* bytes_value = bytes()->Content(decoder, stage);
  uint32_t num_bytes_value = num_bytes()->Value(decoder, stage);
  const zx_handle_t* handles_value = handles()->Content(decoder, stage);
  uint32_t num_handles_value = num_handles()->Value(decoder, stage);
  zx_handle_info_t* handle_infos_value = nullptr;
  if (num_handles_value > 0) {
    handle_infos_value = new zx_handle_info_t[num_handles_value];
    for (uint32_t i = 0; i < num_handles_value; ++i) {
      handle_infos_value[i].handle = handles_value[i];
      handle_infos_value[i].type = ZX_OBJ_TYPE_NONE;
      handle_infos_value[i].rights = 0;
    }
  }
  fidl_codec::DecodedMessage message;
  std::stringstream error_stream;
  message.DecodeMessage(decoder->dispatcher()->MessageDecoderDispatcher(),
                        decoder->fidlcat_thread()->process()->koid(), handle_value, bytes_value,
                        num_bytes_value, handle_infos_value, num_handles_value, type(),
                        error_stream);
  auto result = std::make_unique<fidl_codec::FidlMessageValue>(
      &message, error_stream.str(), bytes_value, num_bytes_value, handle_infos_value,
      num_handles_value);
  delete[] handle_infos_value;
  if (result->is_request()) {
    if (result->matched_request()) {
      decoder->set_semantic(result->method()->semantic());
      decoder->set_decoded_request(result->decoded_request());
    }
    if (result->matched_response()) {
      decoder->set_semantic(result->method()->semantic());
      decoder->set_decoded_response(result->decoded_response());
    }
  }
  return result;
}

std::unique_ptr<fidl_codec::Type> SyscallFidlMessageHandleInfo::ComputeType() const {
  return std::make_unique<fidl_codec::FidlMessageType>();
}

std::unique_ptr<fidl_codec::Value> SyscallFidlMessageHandleInfo::GenerateValue(
    SyscallDecoder* decoder, Stage stage) const {
  zx_handle_t handle_value = handle()->Value(decoder, stage);
  const uint8_t* bytes_value = bytes()->Content(decoder, stage);
  uint32_t num_bytes_value = num_bytes()->Value(decoder, stage);
  const zx_handle_info_t* handle_infos_value = handles()->Content(decoder, stage);
  uint32_t num_handles_value = num_handles()->Value(decoder, stage);
  fidl_codec::DecodedMessage message;
  std::stringstream error_stream;
  message.DecodeMessage(decoder->dispatcher()->MessageDecoderDispatcher(),
                        decoder->fidlcat_thread()->process()->koid(), handle_value, bytes_value,
                        num_bytes_value, handle_infos_value, num_handles_value, type(),
                        error_stream);
  auto result = std::make_unique<fidl_codec::FidlMessageValue>(
      &message, error_stream.str(), bytes_value, num_bytes_value, handle_infos_value,
      num_handles_value);
  if (result->is_request()) {
    if (result->matched_request()) {
      decoder->set_semantic(result->method()->semantic());
      decoder->set_decoded_request(result->decoded_request());
    }
    if (result->matched_response()) {
      decoder->set_semantic(result->method()->semantic());
      decoder->set_decoded_response(result->decoded_response());
    }
  }
  return result;
}

bool ComputeTypes(const std::vector<std::unique_ptr<SyscallInputOutputBase>>& fields,
                  std::vector<std::unique_ptr<fidl_codec::StructMember>>* inline_members,
                  std::vector<std::unique_ptr<fidl_codec::StructMember>>* outline_members) {
  for (const auto& field : fields) {
    std::unique_ptr<fidl_codec::Type> type = field->ComputeType();
    if (type == nullptr) {
      return false;
    }
    if (field->InlineValue()) {
      inline_members->emplace_back(
          std::make_unique<fidl_codec::StructMember>(field->name(), std::move(type), field->id()));
    } else {
      outline_members->emplace_back(
          std::make_unique<fidl_codec::StructMember>(field->name(), std::move(type), field->id()));
    }
  }
  return true;
}

void Syscall::ComputeTypes() {
  fidl_codec_values_ready_ = true;
  if (!fidlcat::ComputeTypes(inputs_, &input_inline_members_, &input_outline_members_)) {
    fidl_codec_values_ready_ = false;
    return;
  }
  if (!fidlcat::ComputeTypes(outputs_, &output_inline_members_, &output_outline_members_)) {
    fidl_codec_values_ready_ = false;
    return;
  }
}

SyscallDecoderDispatcher::SyscallDecoderDispatcher(const DecodeOptions& decode_options)
    : decode_options_(decode_options) {
  Populate();
  ComputeTypes();
  if (!decode_options.trigger_filters.empty()) {
    // We have at least one trigger => wait for a message satisfying the trigger before displaying
    // any syscall.
    display_started_ = false;
  }
  if (!decode_options.message_filters.empty() || !decode_options.exclude_message_filters.empty()) {
    has_filter_ = true;
  }
  if (decode_options.stack_level != kNoStack) {
    needs_stack_frame_ = true;
  }
}

void SyscallDecoderDispatcher::DecodeSyscall(InterceptingThreadObserver* thread_observer,
                                             zxdb::Thread* thread, Syscall* syscall) {
  uint64_t thread_id = thread->GetKoid();
  auto current = syscall_decoders_.find(thread_id);
  if (current != syscall_decoders_.end()) {
    FX_LOGS(ERROR) << thread->GetProcess()->GetName() << ' ' << thread->GetProcess()->GetKoid()
                   << ':' << thread_id << ": Internal error: already decoding the thread";
    return;
  }
  auto decoder = CreateDecoder(thread_observer, thread, syscall);
  auto tmp = decoder.get();
  syscall_decoders_[thread_id] = std::move(decoder);
  tmp->Decode();
}

void SyscallDecoderDispatcher::DecodeException(InterceptionWorkflow* workflow,
                                               zxdb::Thread* thread) {
  uint64_t thread_id = thread->GetKoid();
  auto current = exception_decoders_.find(thread_id);
  if (current != exception_decoders_.end()) {
    FX_LOGS(ERROR) << thread->GetProcess()->GetName() << ' ' << thread->GetProcess()->GetKoid()
                   << ':' << thread_id
                   << ": Internal error: already decoding an exception for the thread";
    return;
  }
  auto decoder = CreateDecoder(workflow, thread);
  auto tmp = decoder.get();
  exception_decoders_[thread_id] = std::move(decoder);
  tmp->Decode();
}

void SyscallDecoderDispatcher::DeleteDecoder(SyscallDecoder* decoder) {
  if (!decoder->aborted()) {
    zxdb::Thread* thread = decoder->get_thread();
    if (thread != nullptr) {
      thread->Continue();
    }
  }
  syscall_decoders_.erase(decoder->fidlcat_thread()->koid());
}

void SyscallDecoderDispatcher::DeleteDecoder(ExceptionDecoder* decoder) {
  zxdb::Thread* thread = decoder->get_thread();
  if (thread != nullptr) {
    thread->Continue();
  }
  exception_decoders_.erase(decoder->thread_id());
}

void SyscallDecoderDispatcher::AddStopMonitoringEvent(std::shared_ptr<StopMonitoringEvent> event) {
  for (const auto& decoder : syscall_decoders_) {
    if (decoder.second->fidlcat_thread()->process() == event->process()) {
      decoder.second->set_aborted();
    }
  }
}

void SyscallDecoderDispatcher::ComputeTypes() {
  for (const auto& syscall : syscalls_) {
    syscall.second->ComputeTypes();
  }
}

std::unique_ptr<SyscallDecoder> SyscallDisplayDispatcher::CreateDecoder(
    InterceptingThreadObserver* thread_observer, zxdb::Thread* thread, const Syscall* syscall) {
  return std::make_unique<SyscallDecoder>(this, thread_observer, thread, syscall,
                                          std::make_unique<SyscallDisplay>(this, os_));
}

std::unique_ptr<ExceptionDecoder> SyscallDisplayDispatcher::CreateDecoder(
    InterceptionWorkflow* workflow, zxdb::Thread* thread) {
  return std::make_unique<ExceptionDecoder>(workflow, this, thread,
                                            std::make_unique<ExceptionDisplay>(this, os_));
}

void SyscallDisplayDispatcher::AddProcessLaunchedEvent(
    std::shared_ptr<ProcessLaunchedEvent> event) {
  last_displayed_syscall_ = nullptr;
  if (event->error_message().empty()) {
    os_ << colors().green << "\nLaunched " << colors().blue << event->command() << colors().reset
        << '\n';
  } else {
    os_ << colors().red << "\nCan't launch " << colors().blue << event->command() << colors().reset
        << " : " << colors().red << event->error_message() << colors().reset << '\n';
  }
}

void SyscallDisplayDispatcher::AddProcessMonitoredEvent(
    std::shared_ptr<ProcessMonitoredEvent> event) {
  last_displayed_syscall_ = nullptr;
  if (event->error_message().empty()) {
    os_ << colors().green << "\nMonitoring ";
  } else {
    os_ << colors().red << "\nCan't monitor ";
  }

  if (event->process()->name().empty()) {
    os_ << colors().reset << "process with koid ";
  } else {
    os_ << colors().blue << event->process()->name() << colors().reset << " koid=";
  }

  os_ << colors().red << event->process()->koid() << colors().reset;
  if (!event->error_message().empty()) {
    os_ << " : " << colors().red << event->error_message() << colors().reset;
  }
  os_ << '\n';
}

void SyscallDisplayDispatcher::AddStopMonitoringEvent(std::shared_ptr<StopMonitoringEvent> event) {
  last_displayed_syscall_ = nullptr;
  os_ << colors().green;
  if (event->process()->name().empty()) {
    os_ << "\nStop monitoring process with koid ";
  } else {
    os_ << "\nStop monitoring " << colors().blue << event->process()->name() << colors().reset
        << " koid=";
  }
  os_ << colors().red << event->process()->koid() << colors().reset << '\n';
  SyscallDecoderDispatcher::AddStopMonitoringEvent(std::move(event));
}

void SyscallDisplayDispatcher::AddInvokedEvent(std::shared_ptr<InvokedEvent> invoked_event) {
  if (!display_started()) {
    // The user specified a trigger. Check if this is a message which satisfies one of the triggers.
    const fidl_codec::FidlMessageValue* message = invoked_event->GetMessage();
    if ((message == nullptr) ||
        !decode_options().IsTrigger(message->method()->fully_qualified_name())) {
      return;
    }
    // We found a trigger => allow the display.
    set_display_started();
  }
  if (has_filter() && invoked_event->syscall()->has_fidl_message()) {
    // We have filters and this is a syscalls with a FIDL message.
    // Only display the syscall if the message satifies the conditions.
    const fidl_codec::FidlMessageValue* message = invoked_event->GetMessage();
    if ((message == nullptr) ||
        !decode_options().SatisfiesMessageFilters(message->method()->fully_qualified_name())) {
      return;
    }
  }
  invoked_event->set_displayed();
  DisplayInvokedEvent(invoked_event.get());
}

void SyscallDisplayDispatcher::DisplayInvokedEvent(const InvokedEvent* invoked_event) {
  std::string line_header = invoked_event->thread()->process()->name() + ' ' + colors().red +
                            std::to_string(invoked_event->thread()->process()->koid()) +
                            colors().reset + ':' + colors().red +
                            std::to_string(invoked_event->thread()->koid()) + colors().reset + ' ';
  if (with_process_info()) {
    os_ << line_header;
  }
  os_ << '\n';

  FidlcatPrinter printer(this, invoked_event->thread()->process()->koid(), os_, line_header);

  // We have been able to create values from the syscall => print them.
  invoked_event->PrettyPrint(printer);
  last_displayed_syscall_ = nullptr;
  last_displayed_event_ = invoked_event;
}

void SyscallDisplayDispatcher::AddOutputEvent(std::shared_ptr<OutputEvent> output_event) {
  if (!output_event->invoked_event()->displayed()) {
    // The display of the syscall wasn't allowed by the input arguments. Check if the output
    // arguments allows its display.
    if (!display_started()) {
      // The user specified a trigger. Check if this is a message which satisfies one of the
      // triggers.
      const fidl_codec::FidlMessageValue* message = output_event->GetMessage();
      if ((message == nullptr) ||
          !decode_options().IsTrigger(message->method()->fully_qualified_name())) {
        return;
      }
      set_display_started();
    }
    if (has_filter() && output_event->syscall()->has_fidl_message()) {
      // We have filters and this is a syscalls with a FIDL message.
      // Only display the syscall if the message satifies the conditions.
      const fidl_codec::FidlMessageValue* message = output_event->GetMessage();
      if ((message == nullptr) ||
          !decode_options().SatisfiesMessageFilters(message->method()->fully_qualified_name())) {
        return;
      }
    }
    // We can display the syscall but the inputs have not been displayed => display the inputs
    // before displaying the outputs.
    DisplayInvokedEvent(output_event->invoked_event());
  }
  if (output_event->syscall()->return_type() != SyscallReturnType::kNoReturn) {
    if (last_displayed_event_ != output_event->invoked_event()) {
      // Add a blank line to tell the user that this display is not linked to the
      // previous displayed lines.
      os_ << "\n";
    }
    std::string line_header;
    if (with_process_info() || (last_displayed_event_ != output_event->invoked_event())) {
      line_header = output_event->thread()->process()->name() + ' ' + colors().red +
                    std::to_string(output_event->thread()->process()->koid()) + colors().reset +
                    ':' + colors().red + std::to_string(output_event->thread()->koid()) +
                    colors().reset + ' ';
    }
    FidlcatPrinter printer(this, output_event->thread()->process()->koid(), os_, line_header);
    // We have been able to create values from the syscall => print them.
    output_event->PrettyPrint(printer);

    last_displayed_syscall_ = nullptr;
    last_displayed_event_ = output_event.get();
  }
}

void SyscallDisplayDispatcher::AddExceptionEvent(std::shared_ptr<ExceptionEvent> exception_event) {
  os_ << '\n';

  std::string line_header =
      exception_event->thread()->process()->name() + ' ' + colors().red +
      std::to_string(exception_event->thread()->process()->koid()) + colors().reset + ':' +
      colors().red + std::to_string(exception_event->thread()->koid()) + colors().reset + ' ';
  FidlcatPrinter printer(this, exception_event->thread()->process()->koid(), os_, line_header);
  exception_event->PrettyPrint(printer);
}

std::unique_ptr<SyscallDecoder> SyscallCompareDispatcher::CreateDecoder(
    InterceptingThreadObserver* thread_observer, zxdb::Thread* thread, const Syscall* syscall) {
  return std::make_unique<SyscallDecoder>(this, thread_observer, thread, syscall,
                                          std::make_unique<SyscallCompare>(this, comparator_, os_));
}

}  // namespace fidlcat
