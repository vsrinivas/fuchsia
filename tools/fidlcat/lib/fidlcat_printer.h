// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_FIDLCAT_PRINTER_H_
#define TOOLS_FIDLCAT_LIB_FIDLCAT_PRINTER_H_

#include <zircon/system/public/zircon/types.h>

#include "src/developer/debug/zxdb/client/process.h"
#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fidl_codec/wire_types.h"
#include "tools/fidlcat/lib/inference.h"
#include "tools/fidlcat/lib/type_decoder.h"

namespace fidlcat {

class Location;
class Process;
class SyscallDisplayDispatcher;

// Printer which allows us to print the infered data for handles.
class FidlcatPrinter : public fidl_codec::PrettyPrinter {
 public:
  FidlcatPrinter(SyscallDisplayDispatcher* dispatcher, Process* process, std::ostream& os,
                 std::string_view line_header, int tabulations = 0);

  bool display_stack_frame() const { return display_stack_frame_; }

  bool DumpMessages() const override { return dump_messages_; }

  void DisplayHandle(const zx_handle_info_t& handle) override;
  void DisplayStatus(zx_status_t status);
  void DisplayInline(
      const std::vector<std::unique_ptr<fidl_codec::StructMember>>& members,
      const std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>>& values);
  void DisplayOutline(
      const std::vector<std::unique_ptr<fidl_codec::StructMember>>& members,
      const std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>>& values);
  void DisplayStackFrame(const std::vector<Location>& stack_frame);

 private:
  const Inference& inference_;
  Process* const process_;
  const bool display_stack_frame_;
  const bool dump_messages_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_FIDLCAT_PRINTER_H_
