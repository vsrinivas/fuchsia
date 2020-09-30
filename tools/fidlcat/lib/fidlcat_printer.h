// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_FIDLCAT_PRINTER_H_
#define TOOLS_FIDLCAT_LIB_FIDLCAT_PRINTER_H_

#include <zircon/types.h>

#include "src/developer/debug/zxdb/client/process.h"
#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fidl_codec/wire_types.h"
#include "tools/fidlcat/lib/inference.h"
#include "tools/fidlcat/lib/type_decoder.h"

namespace fidlcat {

class HandleInfo;
class Location;
class Process;
class SyscallDisplayDispatcher;

// Printer which allows us to print the infered data for handles.
class FidlcatPrinter : public fidl_codec::PrettyPrinter {
 public:
  FidlcatPrinter(SyscallDisplayDispatcher* dispatcher, Process* process, std::ostream& os,
                 const fidl_codec::Colors& colors, std::string_view line_header,
                 int tabulations = 0);
  FidlcatPrinter(SyscallDisplayDispatcher* dispatcher, Process* process, std::ostream& os,
                 std::string_view line_header, int tabulations = 0);

  const Inference& inference() const { return inference_; }

  Process* process() const { return process_; }

  bool display_stack_frame() const { return display_stack_frame_; }

  bool DumpMessages() const override { return dump_messages_; }

  void DisplayHandle(const zx_handle_info_t& handle) override;
  void DisplayHandle(zx_handle_t handle) {
    zx_handle_info_t info = {.handle = handle, .type = 0, .rights = 0};
    DisplayHandle(info);
  }
  void DisplayHandleInfo(HandleInfo* handle_info);
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
