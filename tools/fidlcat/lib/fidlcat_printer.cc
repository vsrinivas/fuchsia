// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/fidlcat_printer.h"

#include "src/lib/fidl_codec/display_handle.h"
#include "tools/fidlcat/lib/event.h"
#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

namespace fidlcat {

FidlcatPrinter::FidlcatPrinter(SyscallDisplayDispatcher* dispatcher, Process* process,
                               std::ostream& os, const fidl_codec::Colors& colors,
                               std::string_view line_header, int tabulations)
    : PrettyPrinter(
          os, colors, dispatcher->message_decoder_dispatcher().display_options().pretty_print,
          line_header, dispatcher->columns(), dispatcher->with_process_info(), tabulations),
      inference_(dispatcher->inference()),
      process_(process),
      display_stack_frame_(dispatcher->decode_options().stack_level != kNoStack),
      dump_messages_(dispatcher->dump_messages()) {}

FidlcatPrinter::FidlcatPrinter(SyscallDisplayDispatcher* dispatcher, Process* process,
                               std::ostream& os, std::string_view line_header, int tabulations)
    : FidlcatPrinter(dispatcher, process, os, dispatcher->colors(), line_header, tabulations) {}

void FidlcatPrinter::DisplayHandle(const zx_handle_disposition_t& handle) {
  HandleInfo* handle_info = process_->SearchHandleInfo(handle.handle);
  if ((handle.type == ZX_OBJ_TYPE_NONE) && (handle_info != nullptr) && (handle.operation == fidl_codec::kNoHandleDisposition)) {
    zx_handle_disposition_t tmp = handle;
    tmp.type = handle_info->object_type();
    fidl_codec::DisplayHandle(tmp, *this);
  } else {
    fidl_codec::DisplayHandle(handle, *this);
  }
  const fidl_codec::semantic::InferredHandleInfo* inferred_handle_info =
      inference_.GetInferredHandleInfo(process_->koid(), handle.handle);
  if (inferred_handle_info != nullptr) {
    (*this) << '(';
    inferred_handle_info->Display(*this);
    (*this) << ')';
  }
}

void FidlcatPrinter::DisplayHandleInfo(HandleInfo* handle_info) {
  zx_handle_disposition_t disposition = {.operation = fidl_codec::kNoHandleDisposition,
                                         .handle = handle_info->handle(),
                                         .type = handle_info->object_type(),
                                         .rights = 0,
                                         .result = ZX_OK};
  fidl_codec::DisplayHandle(disposition, *this);
  const fidl_codec::semantic::InferredHandleInfo* inferred_handle_info =
      inference_.GetInferredHandleInfo(handle_info->thread()->process()->koid(),
                                       handle_info->handle());
  if (inferred_handle_info != nullptr) {
    (*this) << '(';
    inferred_handle_info->Display(*this);
    (*this) << ')';
  }
}

void FidlcatPrinter::DisplayStatus(zx_status_t status) {
  if (status == ZX_OK) {
    (*this) << fidl_codec::Green;
  } else {
    (*this) << fidl_codec::Red;
  }
  (*this) << fidl_codec::StatusName(status) << fidl_codec::ResetColor;
}

void FidlcatPrinter::DisplayInline(
    const std::vector<std::unique_ptr<fidl_codec::StructMember>>& members,
    const std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>>& values) {
  *this << '(';
  const char* separator = "";
  for (const auto& member : members) {
    auto it = values.find(member.get());
    if (it == values.end())
      continue;
    *this << separator << member->name() << ": " << fidl_codec::Green << member->type()->Name()
          << fidl_codec::ResetColor << " = ";
    it->second->PrettyPrint(member->type(), *this);
    separator = ", ";
  }
  *this << ")";
}

void FidlcatPrinter::DisplayOutline(
    const std::vector<std::unique_ptr<fidl_codec::StructMember>>& members,
    const std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>>& values) {
  fidl_codec::Indent indent(*this);
  for (const auto& member : members) {
    auto it = values.find(member.get());
    if (it == values.end())
      continue;
    auto fidl_message_value = it->second->AsFidlMessageValue();
    if (fidl_message_value != nullptr) {
      it->second->PrettyPrint(member->type(), *this);
    } else {
      *this << member->name() << ": " << fidl_codec::Green << member->type()->Name()
            << fidl_codec::ResetColor << " = ";
      it->second->PrettyPrint(member->type(), *this);
      *this << '\n';
    }
  }
}

void FidlcatPrinter::DisplayStackFrame(const std::vector<Location>& stack_frame) {
  bool save_header_on_every_line = header_on_every_line();
  // We want a header on every stack frame line.
  set_header_on_every_line(true);
  for (const auto& location : stack_frame) {
    *this << fidl_codec::YellowBackground << "at " << fidl_codec::Red;
    if (!location.path().empty()) {
      *this << location.path() << fidl_codec::ResetColor << fidl_codec::YellowBackground << ':'
            << fidl_codec::Blue << location.line() << ':' << location.column()
            << fidl_codec::ResetColor;
    } else {
      *this << std::hex << location.address() << fidl_codec::ResetColor << std::dec;
    }
    if (!location.symbol().empty()) {
      *this << ' ' << location.symbol();
    }
    *this << '\n';
  }
  set_header_on_every_line(save_header_on_every_line);
}

}  // namespace fidlcat
