// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/statistics.h"

#include <algorithm>

#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"
#include "tools/fidlcat/proto/session.pb.h"

namespace fidlcat {

void CloseHandleVisitor::VisitHandleValue(const fidl_codec::HandleValue* node,
                                          const fidl_codec::Type* for_type) {
  HandleInfo* handle_info =
      output_event_->thread()->process()->SearchHandleInfo(node->handle().handle);
  if (handle_info != nullptr) {
    handle_info->AddCloseEvent(output_event_);
  }
}

void CreateHandleVisitor::VisitHandleValue(const fidl_codec::HandleValue* node,
                                           const fidl_codec::Type* for_type) {
  HandleInfo* handle_info =
      output_event_->thread()->process()->SearchHandleInfo(node->handle().handle);
  if (handle_info != nullptr) {
    handle_info->AddCreationEvent(output_event_);
  }
}

void SyscallDisplayDispatcher::DisplaySummary(std::ostream& os) {
  const char* separator = "";
  // Displays all the processes one after the other.
  for (const auto& process : processes()) {
    FidlcatPrinter printer(
        this, process.second.get(), os,
        extra_generation_needs_colors() ? fidl_codec::WithColors : fidl_codec::WithoutColors, "");
    printer << separator;
    for (int i = 0; i < columns(); ++i) {
      printer << '-';
    }
    printer << *process.second;
    if (!process.second->handle_infos().empty()) {
      printer << ": " << process.second->handle_infos().size()
              << ((process.second->handle_infos().size() == 1) ? " handle" : " handles");
    }
    printer << '\n';
    fidl_codec::Indent indent(printer);
    // For one process, displays all the handles of the process one after the other.
    for (const auto& handle_info : process.second->handle_infos()) {
      printer << '\n';
      if (handle_info->startup()) {
        printer << fidl_codec::Red << "startup " << fidl_codec::ResetColor;
      }
      if (handle_info->object_type() == ZX_OBJ_TYPE_NONE) {
        printer << fidl_codec::Red << "handle " << fidl_codec::ResetColor;
      }
      printer.DisplayHandleInfo(handle_info);
      printer << '\n';
      {
        fidl_codec::Indent indent(printer);
        bool link_displayed = false;
        zx_handle_t linked_handle =
            inference().GetLinkedHandle(process.second->koid(), handle_info->handle());
        if (linked_handle != ZX_HANDLE_INVALID) {
          // Sometimes, a process creates a pair of channel ends (zx_channel_create).
          // Here, we display the relation between thoise two channel ends.
          printer << "linked to ";
          printer.DisplayHandle(linked_handle);
          printer << '\n';
          link_displayed = true;
        }
        zx_koid_t linked_koid = inference().GetLinkedKoid(handle_info->koid());
        if (linked_koid != ZX_KOID_INVALID) {
          // Sometimes, for channels, we know which process owns the other end. Because the other
          // end may have travelled from one process to another, we may have several processes.
          const std::set<HandleInfo*>* linked_koid_handle_infos =
              inference().GetKoidHandleInfos(linked_koid);
          if (linked_koid_handle_infos != nullptr) {
            for (const auto linked_handle_info : *linked_koid_handle_infos) {
              Process* linked_process = linked_handle_info->thread()->process();
              if (linked_process != process.second.get()) {
                // We only display a relation if it's not in the same process (we already displayed
                // the relation inside the process).
                if (link_displayed) {
                  printer << "which is  ";
                } else {
                  // We haven't displayed yet a relation for the handle (it's not a channel created
                  // by zx_channel_create by the process).
                  printer << "linked to ";
                  link_displayed = true;
                }
                printer.DisplayHandleInfo(linked_handle_info);
                printer << " in process " << linked_process->name() << ':' << fidl_codec::Red
                        << linked_process->koid() << fidl_codec::ResetColor << '\n';
              }
            }
          }
        }
        // Displays all the sessions for the handle.
        const char* separator = "";
        for (const auto& session : handle_info->sessions()) {
          printer << separator;
          const OutputEvent* creation_event = session->creation_event();
          if (creation_event != nullptr) {
            // Displays the creation event for the session.
            printer << "created by ";
            if (creation_event->syscall()->kind() == SyscallKind::kRegularSyscall) {
              // The creation event is something like zx_channel_create, zx_timer_create, ...
              printer << fidl_codec::Green << creation_event->syscall()->name()
                      << fidl_codec::ResetColor;
            } else {
              // The creation event is a message read from a channel (for example zx_channel_read).
              // The process received the event from the message.
              auto creation_handle_info = creation_event->invoked_event()->GetHandleInfo(
                  creation_event->syscall()->SearchInlineMember("handle", /*invoked=*/true));
              FX_DCHECK(creation_handle_info != nullptr);
              printer.DisplayHandleInfo(creation_handle_info);
              const fidl_codec::FidlMessageValue* message = creation_event->GetMessage();
              FX_DCHECK(message != nullptr);
              FX_DCHECK(message->method() != nullptr);
              printer << " receiving " << fidl_codec::Green
                      << message->method()->fully_qualified_name() << fidl_codec::ResetColor;
            }
            printer << '\n';
          }
          // Displays all the regular events for the handle.
          {
            fidl_codec::Indent indent(printer);
            for (const auto& event : session->events()) {
              event->Display(printer);
            }
          }
          const OutputEvent* close_event = session->close_event();
          if (close_event != nullptr) {
            // Displays the close event for the session.
            printer << "closed by ";
            if (close_event->syscall()->kind() == SyscallKind::kRegularSyscall) {
              // The close event is zx_handle_close or zx_handle_close_many.
              printer << fidl_codec::Green << close_event->syscall()->name()
                      << fidl_codec::ResetColor;
            } else {
              // The close event is a message sent to a channel to another process (for example
              // zx_channel_write).
              // The process sent the event using the message.
              auto close_handle_info = close_event->invoked_event()->GetHandleInfo(
                  close_event->syscall()->SearchInlineMember("handle", /*invoked=*/true));
              FX_DCHECK(close_handle_info != nullptr);
              printer.DisplayHandleInfo(close_handle_info);
              const fidl_codec::FidlMessageValue* message =
                  close_event->invoked_event()->GetMessage();
              FX_DCHECK(message != nullptr);
              FX_DCHECK(message->method() != nullptr);
              printer << " sending " << fidl_codec::Green
                      << message->method()->fully_qualified_name() << fidl_codec::ResetColor;
            }
            printer << '\n';
          }
          separator = "\n";
        }
      }
    }
    separator = "\n";
  }
}

}  // namespace fidlcat
