// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/event.h"

#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

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
              // Associate the koid to the handle only if the handle is currently used by the
              // monitored process. That is if the handle if referenced by an event.
              // That means that we may need an extra load if the handle is already known by the
              // kernel but not yet needed by the monitored process. This way we avoid creating
              // handle description for handle we don't know the semantic.
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

void InvokedEvent::PrettyPrint(FidlcatPrinter& printer) const {
  if (printer.display_stack_frame()) {
    printer.DisplayStackFrame(stack_frame_);
  }
  printer << syscall()->name();
  printer.DisplayInline(syscall()->input_inline_members(), inline_fields());
  printer << '\n';
  printer.DisplayOutline(syscall()->input_outline_members(), outline_fields());
}

void OutputEvent::PrettyPrint(FidlcatPrinter& printer) const {
  fidl_codec::Indent indent(printer);
  if (!printer.DisplayReturnedValue(syscall()->return_type(), returned_value_)) {
    return;
  }
  // Adds the inline output arguments (if any).
  if (!inline_fields().empty()) {
    printer << ' ';
    printer.DisplayInline(syscall()->output_inline_members(), inline_fields());
  }
  printer << '\n';
  printer.DisplayOutline(syscall()->output_outline_members(), outline_fields());
}

void ExceptionEvent::PrettyPrint(FidlcatPrinter& printer) const {
  printer.DisplayStackFrame(stack_frame_);
  printer << fidl_codec::Red << "thread stopped on exception" << fidl_codec::ResetColor << '\n';
}

}  // namespace fidlcat
