// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/event.h"

#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

namespace fidlcat {

void FidlcatPrinter::DisplayHandle(const zx_handle_info_t& handle) {
  dispatcher_->DisplayHandle(handle, colors(), os());
}

void InvokedEvent::PrettyPrint(fidl_codec::PrettyPrinter& printer) {
  printer << syscall_->name() << '(';
  const char* separator = "";
  for (const auto& member : syscall_->input_inline_members()) {
    auto it = inline_fields_.find(member.get());
    if (it == inline_fields_.end())
      continue;
    printer << separator << member->name() << ":" << fidl_codec::Green << member->type()->Name()
            << fidl_codec::ResetColor << ": ";
    it->second->PrettyPrint(member->type(), printer);
    separator = ", ";
  }
  printer << ")\n";
  // Currently we can only have handle values which are inline.
  FXL_DCHECK(syscall_->input_outline_members().empty());
}

}  // namespace fidlcat
