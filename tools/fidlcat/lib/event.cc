// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/event.h"

#include "src/lib/fidl_codec/proto_value.h"
#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"
#include "tools/fidlcat/proto/session.pb.h"

namespace fidlcat {

void Process::LoadHandleInfo(Inference* inference) {
  zxdb::Process* zxdb_process = zxdb_process_.get();
  if (zxdb_process == nullptr) {
    return;
  }
  if (loading_handle_info_) {
    // We are currently loading information about the handles. If we are unlucky, the result won't
    // include information about handles we are now needing. Ask the process to do another load just
    // after the current one to be sure to have all the handles we need (including the handle only
    // needed after the start of the load).
    needs_to_load_handle_info_ = true;
    return;
  }
  loading_handle_info_ = true;
  needs_to_load_handle_info_ = false;
  zxdb_process->LoadInfoHandleTable(
      [this, inference](zxdb::ErrOr<std::vector<debug_ipc::InfoHandleExtended>> handles) {
        loading_handle_info_ = false;
        if (!handles.ok()) {
          FX_LOGS(ERROR) << "msg: " << handles.err().msg();
        } else {
          for (const auto& handle : handles.value()) {
            HandleInfo* handle_info = SearchHandleInfo(handle.handle_value);
            if (handle_info != nullptr) {
              // Associate the koid and the object type to the handle only if the handle is
              // currently used by the monitored process. That is if the handle if referenced by an
              // event.
              // That means that we may need an extra load if the handle is already known by the
              // kernel but not yet needed by the monitored process. This way we avoid creating
              // a Handle object for handles we don't know the semantic.
              handle_info->set_object_type(handle.type);
              handle_info->set_rights(handle.rights);
              handle_info->set_koid(handle.koid);
              inference->AddKoidHandleInfo(handle.koid, handle_info);
            }
            if (handle.related_koid != ZX_HANDLE_INVALID) {
              // However, the association of koids is always useful.
              inference->AddLinkedKoids(handle.koid, handle.related_koid);
            }
          }
          if (needs_to_load_handle_info_) {
            needs_to_load_handle_info_ = false;
            LoadHandleInfo(inference);
          }
        }
      });
}

void Protocol::AddEvent(const OutputEvent* event, const fidl_codec::FidlMessageValue* message) {
  Method* method = GetMethod(message->ordinal(), message->method());
  method->AddEvent(event);
  ++event_count_;
}

void Process::AddEvent(const OutputEvent* event, const fidl_codec::FidlMessageValue* message) {
  Protocol* protocol = GetProtocol(
      (message->method() != nullptr) ? &message->method()->enclosing_interface() : nullptr);
  protocol->AddEvent(event, message);
  ++event_count_;
}

void ProcessLaunchedEvent::Write(proto::Event* dst) const {
  dst->set_timestamp(timestamp());
  proto::ProcessLaunchedEvent* event = dst->mutable_process_launched();
  event->set_command(command());
  event->set_error_message(error_message());
}

void ProcessMonitoredEvent::Write(proto::Event* dst) const {
  dst->set_timestamp(timestamp());
  proto::ProcessMonitoredEvent* event = dst->mutable_process_monitored();
  event->set_process_koid(process()->koid());
  event->set_error_message(error_message());
}

void StopMonitoringEvent::Write(proto::Event* dst) const {
  dst->set_timestamp(timestamp());
  proto::StopMonitoringEvent* event = dst->mutable_stop_monitoring();
  event->set_process_koid(process()->koid());
}

bool SyscallEvent::NeedsToLoadHandleInfo(Inference* inference) {
  for (const auto& field : inline_fields_) {
    if (field.second->NeedsToLoadHandleInfo(thread()->koid(), inference)) {
      return true;
    }
  }
  for (const auto& field : outline_fields_) {
    if (field.second->NeedsToLoadHandleInfo(thread()->koid(), inference)) {
      return true;
    }
  }
  return false;
}

const fidl_codec::FidlMessageValue* SyscallEvent::GetMessage() const {
  if (outline_fields_.size() == 0) {
    return nullptr;
  }
  return outline_fields_.begin()->second->AsFidlMessageValue();
}

const fidl_codec::Value* SyscallEvent::GetValue(const fidl_codec::StructMember* member) const {
  if (member == nullptr) {
    return nullptr;
  }
  auto result = inline_fields_.find(member);
  if (result != inline_fields_.end()) {
    return result->second.get();
  }
  auto result2 = outline_fields_.find(member);
  if (result2 != outline_fields_.end()) {
    return result2->second.get();
  }
  return nullptr;
}

const fidl_codec::HandleValue* SyscallEvent::GetHandleValue(
    const fidl_codec::StructMember* member) const {
  if (member == nullptr) {
    return nullptr;
  }
  auto result = inline_fields_.find(member);
  if (result == inline_fields_.end()) {
    return nullptr;
  }
  return result->second->AsHandleValue();
}

HandleInfo* SyscallEvent::GetHandleInfo(const fidl_codec::StructMember* member) const {
  if (member == nullptr) {
    return nullptr;
  }
  auto result = inline_fields_.find(member);
  if (result == inline_fields_.end()) {
    return nullptr;
  }
  const fidl_codec::HandleValue* value = result->second->AsHandleValue();
  if (value == nullptr) {
    return nullptr;
  }
  return thread()->process()->SearchHandleInfo(value->handle().handle);
}

void InvokedEvent::ComputeHandleInfo(SyscallDisplayDispatcher* dispatcher) {
  switch (syscall()->kind()) {
    case SyscallKind::kChannelRead:
    case SyscallKind::kChannelWrite:
    case SyscallKind::kChannelCall: {
      // Compute the handle which is used to read/write a message.
      FX_DCHECK(!syscall()->input_inline_members().empty());
      auto value = inline_fields().find(syscall()->input_inline_members()[0].get());
      FX_DCHECK(value != inline_fields().end());
      handle_info_ =
          thread()->process()->SearchHandleInfo(value->second->AsHandleValue()->handle().handle);
      if (handle_info_ == nullptr) {
        handle_info_ = dispatcher->CreateHandleInfo(
            thread(), value->second->AsHandleValue()->handle().handle, 0, false);
      }
      break;
    }
    default:
      break;
  }
}

void InvokedEvent::Write(proto::Event* dst) const {
  dst->set_timestamp(timestamp());
  proto::InvokedEvent* event = dst->mutable_invoked();
  event->set_thread_koid(thread()->koid());
  for (const auto& location : stack_frame_) {
    proto::Location* proto_location = event->add_frame();
    proto_location->set_path(location.path());
    proto_location->set_line(location.line());
    proto_location->set_column(location.column());
    proto_location->set_address(location.address());
    proto_location->set_symbol(location.symbol());
  }
  event->set_syscall(syscall()->name());
  for (const auto& field : inline_fields()) {
    fidl_codec::proto::Value value;
    fidl_codec::ProtoVisitor visitor(&value);
    field.second->Visit(&visitor, nullptr);
    if (field.first->id() != 0) {
      event->mutable_inline_id_fields()->insert(
          google::protobuf::MapPair(static_cast<uint32_t>(field.first->id()), value));
    } else {
      event->mutable_inline_fields()->insert(google::protobuf::MapPair(field.first->name(), value));
    }
  }
  for (const auto& field : outline_fields()) {
    fidl_codec::proto::Value value;
    fidl_codec::ProtoVisitor visitor(&value);
    field.second->Visit(&visitor, nullptr);
    if (field.first->id() != 0) {
      event->mutable_outline_id_fields()->insert(
          google::protobuf::MapPair(static_cast<uint32_t>(field.first->id()), value));
    } else {
      event->mutable_outline_fields()->insert(
          google::protobuf::MapPair(field.first->name(), value));
    }
  }
}

void InvokedEvent::PrettyPrint(FidlcatPrinter& printer) const {
  if (printer.display_stack_frame()) {
    printer.DisplayStackFrame(stack_frame_);
  }
  printer << syscall()->name();
  printer.DisplayInline(syscall()->input_inline_members(), inline_fields());
  printer << '\n';
  printer.DisplayOutline(syscall()->input_outline_members(), outline_fields());
}

void OutputEvent::Write(proto::Event* dst) const {
  dst->set_timestamp(timestamp());
  proto::OutputEvent* event = dst->mutable_output();
  event->set_thread_koid(thread()->koid());
  event->set_syscall(syscall()->name());
  event->set_returned_value(returned_value());
  event->set_invoked_event_id(invoked_event()->id());
  for (const auto& field : inline_fields()) {
    fidl_codec::proto::Value value;
    fidl_codec::ProtoVisitor visitor(&value);
    field.second->Visit(&visitor, nullptr);
    if (field.first->id() != 0) {
      event->mutable_inline_id_fields()->insert(
          google::protobuf::MapPair(static_cast<uint32_t>(field.first->id()), value));
    } else {
      event->mutable_inline_fields()->insert(google::protobuf::MapPair(field.first->name(), value));
    }
  }
  for (const auto& field : outline_fields()) {
    fidl_codec::proto::Value value;
    fidl_codec::ProtoVisitor visitor(&value);
    field.second->Visit(&visitor, nullptr);
    if (field.first->id() != 0) {
      event->mutable_outline_id_fields()->insert(
          google::protobuf::MapPair(static_cast<uint32_t>(field.first->id()), value));
    } else {
      event->mutable_outline_fields()->insert(
          google::protobuf::MapPair(field.first->name(), value));
    }
  }
}

void OutputEvent::Display(FidlcatPrinter& printer, bool with_channel) const {
  const fidl_codec::FidlMessageValue* message = invoked_event_->GetMessage();
  if (message == nullptr) {
    message = GetMessage();
    if (message == nullptr) {
      return;
    }
  }
  switch (syscall()->kind()) {
    case SyscallKind::kChannelRead:
      printer << "read  ";
      break;
    case SyscallKind::kChannelWrite:
      printer << "write ";
      break;
    case SyscallKind::kChannelCall:
      printer << "call  ";
      break;
    default:
      return;
  }
  const fidl_codec::InterfaceMethod* method = message->method();
  if (message->ordinal() == kFidlOrdinalEpitaph) {
    printer << fidl_codec::WhiteOnMagenta << "epitaph " << fidl_codec::ResetColor << ' '
            << ((message->epitaph_error() == "ZX_OK") ? fidl_codec::Green : fidl_codec::Red)
            << message->epitaph_error() << fidl_codec::ResetColor;
  } else {
    if (method == nullptr) {
      printer << " ordinal=" << std::hex << message->ordinal() << std::dec;
    } else {
      printer << fidl_codec::WhiteOnMagenta
              << (message->is_request()
                      ? "request "
                      : ((method->request() != nullptr) ? "response" : "event   "))
              << fidl_codec::ResetColor << ' ' << fidl_codec::Green
              << method->enclosing_interface().name() << '.' << method->name()
              << fidl_codec::ResetColor;
    }
  }
  bool first_argument = true;
  if (with_channel && (invoked_event()->handle_info() != nullptr)) {
    printer << '(';
    printer.DisplayHandleInfo(invoked_event()->handle_info());
    first_argument = false;
  }
  if ((method != nullptr) && (method->short_display() != nullptr)) {
    fidl_codec::Indent indent(printer);
    const fidl_codec::StructValue* request = (syscall()->kind() == SyscallKind::kChannelRead)
                                                 ? GetMessage()->decoded_request()
                                                 : invoked_event()->GetMessage()->decoded_request();
    fidl_codec::semantic::SemanticContext context(&printer.inference(), printer.process()->koid(),
                                                  (invoked_event()->handle_info() == nullptr)
                                                      ? ZX_HANDLE_INVALID
                                                      : invoked_event()->handle_info()->handle(),
                                                  request, nullptr);
    for (const auto& expression : method->short_display()->inputs()) {
      if (first_argument) {
        printer << '(';
        first_argument = false;
      } else {
        printer << ", ";
      }
      expression->PrettyPrint(printer, &context);
    }
  }
  if (!first_argument) {
    printer << ')';
  }
  printer << '\n';
  if ((method != nullptr) && (method->short_display() != nullptr)) {
    fidl_codec::Indent indent(printer);
    const fidl_codec::StructValue* request = (syscall()->kind() == SyscallKind::kChannelRead)
                                                 ? GetMessage()->decoded_request()
                                                 : invoked_event()->GetMessage()->decoded_request();
    fidl_codec::semantic::SemanticContext context(&printer.inference(), printer.process()->koid(),
                                                  (invoked_event()->handle_info() == nullptr)
                                                      ? ZX_HANDLE_INVALID
                                                      : invoked_event()->handle_info()->handle(),
                                                  request, nullptr);
    bool first_result = true;
    for (const auto& expression : method->short_display()->results()) {
      printer << (first_result ? "-> " : ", ");
      first_result = false;
      expression->PrettyPrint(printer, &context);
    }
    if (!first_result) {
      printer << '\n';
    }
  }
}

void OutputEvent::PrettyPrint(FidlcatPrinter& printer) const {
  fidl_codec::Indent indent(printer);

  switch (syscall()->return_type()) {
    case SyscallReturnType::kNoReturn:
      return;
    case SyscallReturnType::kVoid:
      if (inline_fields().empty() && outline_fields().empty()) {
        return;
      }
      printer << "-> ";
      break;
    case SyscallReturnType::kStatus:
      printer << "-> ";
      printer.DisplayStatus(static_cast<zx_status_t>(returned_value_));
      break;
    case SyscallReturnType::kTicks:
      printer << "-> " << fidl_codec::Green << "ticks" << fidl_codec::ResetColor << ": "
              << fidl_codec::Blue << static_cast<uint64_t>(returned_value_)
              << fidl_codec::ResetColor;
      break;
    case SyscallReturnType::kTime:
      printer << "-> " << fidl_codec::Green << "time" << fidl_codec::ResetColor << ": ";
      printer.DisplayTime(static_cast<zx_time_t>(returned_value_));
      break;
    case SyscallReturnType::kUint32:
      printer << "-> " << fidl_codec::Blue << static_cast<uint32_t>(returned_value_)
              << fidl_codec::ResetColor;
      break;
    case SyscallReturnType::kUint64:
      printer << "-> " << fidl_codec::Blue << static_cast<uint64_t>(returned_value_)
              << fidl_codec::ResetColor;
      break;
  }
  // Adds the inline output arguments (if any).
  if (!inline_fields().empty()) {
    printer << ' ';
    printer.DisplayInline(syscall()->output_inline_members(), inline_fields());
  }
  printer << '\n';
  printer.DisplayOutline(syscall()->output_outline_members(), outline_fields());
}

void ExceptionEvent::Write(proto::Event* dst) const {
  dst->set_timestamp(timestamp());
  proto::ExceptionEvent* event = dst->mutable_exception();
  event->set_thread_koid(thread()->koid());
  for (const auto& location : stack_frame_) {
    proto::Location* proto_location = event->add_frame();
    proto_location->set_path(location.path());
    proto_location->set_line(location.line());
    proto_location->set_column(location.column());
    proto_location->set_address(location.address());
    proto_location->set_symbol(location.symbol());
  }
}

void ExceptionEvent::PrettyPrint(FidlcatPrinter& printer) const {
  printer.DisplayStackFrame(stack_frame_);
  printer << fidl_codec::Red << "thread stopped on exception" << fidl_codec::ResetColor << '\n';
}

bool EventDecoder::DecodeAndDispatchEvent(const proto::Event& proto_event) {
  switch (proto_event.Kind_case()) {
    case proto::Event::kProcessLaunched: {
      const proto::ProcessLaunchedEvent& content = proto_event.process_launched();
      dispatcher_->AddProcessLaunchedEvent(std::make_shared<ProcessLaunchedEvent>(
          proto_event.timestamp(), content.command(), content.error_message()));
      return true;
    }
    case proto::Event::kProcessMonitored: {
      const proto::ProcessMonitoredEvent& content = proto_event.process_monitored();
      Process* process = dispatcher_->SearchProcess(content.process_koid());
      if (process == nullptr) {
        FX_LOGS(ERROR) << "Process " << content.process_koid() << " not found for event .";
        return false;
      }
      dispatcher_->AddProcessMonitoredEvent(std::make_shared<ProcessMonitoredEvent>(
          proto_event.timestamp(), process, content.error_message()));
      return true;
    }
    case proto::Event::kStopMonitoring: {
      const proto::StopMonitoringEvent& content = proto_event.stop_monitoring();
      Process* process = dispatcher_->SearchProcess(content.process_koid());
      if (process == nullptr) {
        FX_LOGS(ERROR) << "Process " << content.process_koid() << " not found for event .";
        return false;
      }
      dispatcher_->AddStopMonitoringEvent(
          std::make_shared<StopMonitoringEvent>(proto_event.timestamp(), process));
      return true;
    }
    case proto::Event::kInvoked: {
      const proto::InvokedEvent& content = proto_event.invoked();
      Thread* thread = dispatcher_->SearchThread(content.thread_koid());
      if (thread == nullptr) {
        FX_LOGS(ERROR) << "Thread " << content.thread_koid() << " not found for event.";
        return false;
      }
      Syscall* syscall = dispatcher_->SearchSyscall(content.syscall());
      if (syscall == nullptr) {
        FX_LOGS(ERROR) << "Syscall " << content.syscall() << " not found.";
        return false;
      }
      auto event = std::make_shared<InvokedEvent>(proto_event.timestamp(), thread, syscall);
      if (!DecodeValues(event.get(), content.inline_fields(), content.inline_id_fields(),
                        content.outline_fields(), content.outline_id_fields(),
                        /*invoked=*/true)) {
        return false;
      }
      for (int index = 0; index < content.frame_size(); ++index) {
        const proto::Location& proto_location = content.frame(index);
        event->stack_frame().emplace_back(proto_location.path(), proto_location.line(),
                                          proto_location.column(), proto_location.address(),
                                          proto_location.symbol());
      }
      invoked_events_.emplace(std::make_pair(invoked_events_.size(), event));
      dispatcher_->AddInvokedEvent(std::move(event));
      return true;
    }
    case proto::Event::kOutput: {
      const proto::OutputEvent& content = proto_event.output();
      Thread* thread = dispatcher_->SearchThread(content.thread_koid());
      if (thread == nullptr) {
        FX_LOGS(ERROR) << "Thread " << content.thread_koid() << " not found for event.";
        return false;
      }
      Syscall* syscall = dispatcher_->SearchSyscall(content.syscall());
      if (syscall == nullptr) {
        FX_LOGS(ERROR) << "Syscall " << content.syscall() << " not found.";
        return false;
      }
      auto invoked_event = invoked_events_.find(content.invoked_event_id());
      if (invoked_event == invoked_events_.end()) {
        FX_LOGS(ERROR) << "Invoked event " << content.invoked_event_id()
                       << " not found for ouput event.";
        return false;
      }
      auto event = std::make_shared<OutputEvent>(proto_event.timestamp(), thread, syscall,
                                                 content.returned_value(), invoked_event->second);
      if (!DecodeValues(event.get(), content.inline_fields(), content.inline_id_fields(),
                        content.outline_fields(), content.outline_id_fields(),
                        /*invoked=*/false)) {
        return false;
      }
      dispatcher_->AddOutputEvent(std::move(event));
      return true;
    }
    case proto::Event::kException: {
      const proto::ExceptionEvent& content = proto_event.exception();
      Thread* thread = dispatcher_->SearchThread(content.thread_koid());
      if (thread == nullptr) {
        FX_LOGS(ERROR) << "Thread " << content.thread_koid() << " not found for event.";
        return false;
      }
      auto event = std::make_shared<ExceptionEvent>(proto_event.timestamp(), thread);
      for (int index = 0; index < content.frame_size(); ++index) {
        const proto::Location& proto_location = content.frame(index);
        event->stack_frame().emplace_back(proto_location.path(), proto_location.line(),
                                          proto_location.column(), proto_location.address(),
                                          proto_location.symbol());
      }
      dispatcher_->AddExceptionEvent(std::move(event));
      return true;
    }
    default:
      FX_LOGS(ERROR) << "Bad kind for event.";
      return false;
  }
}

bool EventDecoder::DecodeValues(
    SyscallEvent* event,
    const ::google::protobuf::Map<::std::string, ::fidl_codec::proto::Value>& inline_fields,
    const ::google::protobuf::Map<uint32_t, ::fidl_codec::proto::Value>& inline_id_fields,
    const ::google::protobuf::Map<::std::string, ::fidl_codec::proto::Value>& outline_fields,
    const ::google::protobuf::Map<uint32_t, ::fidl_codec::proto::Value>& outline_id_fields,
    bool invoked) {
  bool ok = true;
  for (const auto& proto_value : inline_fields) {
    const fidl_codec::StructMember* member =
        event->syscall()->SearchInlineMember(proto_value.first, invoked);
    if (member == nullptr) {
      FX_LOGS(ERROR) << "Member " << proto_value.first << " not found for "
                     << event->syscall()->name() << '.';
      ok = false;
    } else {
      std::unique_ptr<fidl_codec::Value> value =
          fidl_codec::DecodeValue(dispatcher_->loader(), proto_value.second, member->type());
      if (value == nullptr) {
        ok = false;
      } else {
        event->AddInlineField(member, std::move(value));
      }
    }
  }
  for (const auto& proto_value : inline_id_fields) {
    const fidl_codec::StructMember* member =
        event->syscall()->SearchInlineMember(proto_value.first, invoked);
    if (member == nullptr) {
      FX_LOGS(ERROR) << "Member " << proto_value.first << " not found for "
                     << event->syscall()->name() << '.';
      ok = false;
    } else {
      std::unique_ptr<fidl_codec::Value> value =
          fidl_codec::DecodeValue(dispatcher_->loader(), proto_value.second, member->type());
      if (value == nullptr) {
        ok = false;
      } else {
        event->AddInlineField(member, std::move(value));
      }
    }
  }
  for (const auto& proto_value : outline_fields) {
    const fidl_codec::StructMember* member =
        event->syscall()->SearchOutlineMember(proto_value.first, invoked);
    if (member == nullptr) {
      FX_LOGS(ERROR) << "Member " << proto_value.first << " not found for "
                     << event->syscall()->name() << '.';
      ok = false;
    } else {
      std::unique_ptr<fidl_codec::Value> value =
          fidl_codec::DecodeValue(dispatcher_->loader(), proto_value.second, member->type());
      if (value == nullptr) {
        ok = false;
      } else {
        event->AddOutlineField(member, std::move(value));
      }
    }
  }
  for (const auto& proto_value : outline_id_fields) {
    const fidl_codec::StructMember* member =
        event->syscall()->SearchOutlineMember(proto_value.first, invoked);
    if (member == nullptr) {
      FX_LOGS(ERROR) << "Member " << proto_value.first << " not found for "
                     << event->syscall()->name() << '.';
      ok = false;
    } else {
      std::unique_ptr<fidl_codec::Value> value =
          fidl_codec::DecodeValue(dispatcher_->loader(), proto_value.second, member->type());
      if (value == nullptr) {
        ok = false;
      } else {
        event->AddOutlineField(member, std::move(value));
      }
    }
  }
  return ok;
}

}  // namespace fidlcat
