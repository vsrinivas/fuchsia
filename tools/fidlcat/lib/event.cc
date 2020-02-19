// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/event.h"

#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

namespace fidlcat {

void FidlcatPrinter::DisplayHandle(const zx_handle_info_t& handle) {
  decoder_->DisplayHandle(handle, colors(), os());
}

void FidlcatPrinter::DisplayStatus(zx_status_t status) {
  if (status == ZX_OK) {
    (*this) << fidl_codec::Green;
  } else {
    (*this) << fidl_codec::Red;
  }
  (*this) << fidl_codec::StatusName(status) << fidl_codec::ResetColor;
}

void FidlcatPrinter::DisplayTime(zx_time_t time_ns) {
  if (time_ns == ZX_TIME_INFINITE) {
    (*this) << fidl_codec::Blue << "ZX_TIME_INFINITE" << fidl_codec::ResetColor;
  } else if (time_ns == ZX_TIME_INFINITE_PAST) {
    (*this) << fidl_codec::Blue << "ZX_TIME_INFINITE_PAST" << fidl_codec::ResetColor;
  } else {
    // Gets the time in seconds.
    time_t value = time_ns / kOneBillion;
    struct tm tm;
    if (localtime_r(&value, &tm) == &tm) {
      char buffer[100];
      strftime(buffer, sizeof(buffer), "%c", &tm);
      // And now, displays the nano seconds.
      (*this) << fidl_codec::Blue << buffer << " and ";
      snprintf(buffer, sizeof(buffer), "%09" PRId64, time_ns % kOneBillion);
      (*this) << buffer << " ns" << fidl_codec::ResetColor;
    } else {
      (*this) << fidl_codec::Red << "unknown time" << fidl_codec::ResetColor;
    }
  }
}

bool FidlcatPrinter::DisplayReturnedValue(SyscallReturnType type, int64_t returned_value) {
  switch (type) {
    case SyscallReturnType::kNoReturn:
    case SyscallReturnType::kVoid:
      return false;
    case SyscallReturnType::kStatus:
      (*this) << "-> ";
      DisplayStatus(static_cast<zx_status_t>(returned_value));
      break;
    case SyscallReturnType::kTicks:
      (*this) << "-> " << fidl_codec::Green << "ticks" << fidl_codec::ResetColor << ": "
              << fidl_codec::Blue << static_cast<uint64_t>(returned_value)
              << fidl_codec::ResetColor;
      break;
    case SyscallReturnType::kTime:
      (*this) << "-> " << fidl_codec::Green << "time" << fidl_codec::ResetColor << ": ";
      DisplayTime(static_cast<zx_time_t>(returned_value));
      break;
    case SyscallReturnType::kUint32:
      (*this) << "-> " << fidl_codec::Blue << static_cast<uint32_t>(returned_value)
              << fidl_codec::ResetColor;
      break;
    case SyscallReturnType::kUint64:
      (*this) << "-> " << fidl_codec::Blue << static_cast<uint64_t>(returned_value)
              << fidl_codec::ResetColor;
      break;
  }
  return true;
}

void FidlcatPrinter::DisplayInline(
    const std::vector<std::unique_ptr<fidl_codec::StructMember>>& members,
    const std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>>& values) {
  (*this) << '(';
  const char* separator = "";
  for (const auto& member : members) {
    auto it = values.find(member.get());
    if (it == values.end())
      continue;
    (*this) << separator << member->name() << ":" << fidl_codec::Green << member->type()->Name()
            << fidl_codec::ResetColor << ": ";
    it->second->PrettyPrint(member->type(), *this);
    separator = ", ";
  }
  (*this) << ")";
}

void InvokedEvent::PrettyPrint(FidlcatPrinter& printer) {
  printer << syscall_->name();
  printer.DisplayInline(syscall_->input_inline_members(), inline_fields_);
  printer << '\n';
  // Currently we can only have handle values which are inline.
  FXL_DCHECK(syscall_->input_outline_members().empty());
}

void OutputEvent::PrettyPrint(FidlcatPrinter& printer) {
  fidl_codec::Indent indent(printer);
  if (!printer.DisplayReturnedValue(syscall_->return_type(), returned_value_)) {
    return;
  }
  // Adds the inline output arguments (if any).
  if (!syscall_->output_inline_members().empty()) {
    printer << ' ';
    printer.DisplayInline(syscall_->output_inline_members(), inline_fields_);
  }
  printer << '\n';
  // Currently we can only have handle values which are inline.
  FXL_DCHECK(syscall_->output_outline_members().empty());
}

}  // namespace fidlcat
