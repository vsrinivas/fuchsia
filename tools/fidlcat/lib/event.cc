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
            fidl_codec::semantic::HandleDescription* description =
                inference->GetHandleDescription(koid_, handle.handle_value);
            if (description != nullptr) {
              // Associate the koid and the object type to the handle only if the handle is
              // currently used by the monitored process. That is if the handle if referenced by an
              // event.
              // That means that we may need an extra load if the handle is already known by the
              // kernel but not yet needed by the monitored process. This way we avoid creating
              // handle description for handles we don't know the semantic.
              description->set_object_type(handle.type);
              description->set_rights(handle.rights);
              description->set_koid(handle.koid);
            }
            if (handle.related_koid != ZX_HANDLE_INVALID) {
              // However, the associated of koids is always useful.
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

bool SyscallEvent::NeedsToLoadHandleInfo(zx_koid_t pid, Inference* inference) {
  for (const auto& field : inline_fields_) {
    if (field.second->NeedsToLoadHandleInfo(pid, inference)) {
      return true;
    }
  }
  for (const auto& field : outline_fields_) {
    if (field.second->NeedsToLoadHandleInfo(pid, inference)) {
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
    event->mutable_inline_fields()->insert(google::protobuf::MapPair(field.first->name(), value));
  }
  for (const auto& field : outline_fields()) {
    fidl_codec::proto::Value value;
    fidl_codec::ProtoVisitor visitor(&value);
    field.second->Visit(&visitor, nullptr);
    event->mutable_outline_fields()->insert(google::protobuf::MapPair(field.first->name(), value));
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
    event->mutable_inline_fields()->insert(google::protobuf::MapPair(field.first->name(), value));
  }
  for (const auto& field : outline_fields()) {
    fidl_codec::proto::Value value;
    fidl_codec::ProtoVisitor visitor(&value);
    field.second->Visit(&visitor, nullptr);
    event->mutable_outline_fields()->insert(google::protobuf::MapPair(field.first->name(), value));
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
      if (!DecodeValues(event.get(), content.inline_fields(), content.outline_fields(),
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
      if (!DecodeValues(event.get(), content.inline_fields(), content.outline_fields(),
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
    const ::google::protobuf::Map<::std::string, ::fidl_codec::proto::Value>& outline_fields,
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
  return ok;
}

}  // namespace fidlcat
