// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_EVENT_H_
#define TOOLS_FIDLCAT_LIB_EVENT_H_

#include <zircon/system/public/zircon/types.h>

#include "src/developer/debug/zxdb/client/process.h"
#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fidl_codec/wire_types.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "tools/fidlcat/lib/inference.h"
#include "tools/fidlcat/lib/type_decoder.h"

namespace fidlcat {

class Syscall;
class SyscallDecoder;

// Printer which allows us to print the infered data for handles.
class FidlcatPrinter : public fidl_codec::PrettyPrinter {
 public:
  FidlcatPrinter(SyscallDecoder* decoder, bool dump_messages, bool pretty_print, std::ostream& os,
                 const fidl_codec::Colors& colors, std::string_view line_header, int max_line_size,
                 bool header_on_every_line, int tabulations = 0)
      : PrettyPrinter(os, colors, pretty_print, line_header, max_line_size, header_on_every_line,
                      tabulations),
        decoder_(decoder),
        dump_messages_(dump_messages) {}

  bool DumpMessages() const override { return dump_messages_; }

  void DisplayHandle(const zx_handle_info_t& handle) override;
  void DisplayStatus(zx_status_t status);
  bool DisplayReturnedValue(SyscallReturnType type, int64_t returned_value);
  void DisplayInline(
      const std::vector<std::unique_ptr<fidl_codec::StructMember>>& members,
      const std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>>& values);
  void DisplayOutline(
      const std::vector<std::unique_ptr<fidl_codec::StructMember>>& members,
      const std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>>& values);

 private:
  SyscallDecoder* decoder_;
  const bool dump_messages_;
};

class Process {
 public:
  Process(std::string_view name, zx_koid_t koid, fxl::WeakPtr<zxdb::Process> zxdb_process)
      : name_(name), koid_(koid), zxdb_process_(zxdb_process) {}

  const std::string& name() const { return name_; }
  zx_koid_t koid() const { return koid_; }
  zxdb::Process* zxdb_process() const { return zxdb_process_.get(); }

  void LoadHandleInfo(Inference* inference);

 private:
  // The name of the process.
  const std::string name_;
  // The koid of the process.
  const zx_koid_t koid_;
  // The zxdb process for the koid.
  fxl::WeakPtr<zxdb::Process> zxdb_process_;
  // True if we are currently loading information about the process' handles.
  bool loading_handle_info_ = false;
  // True if we need to load again the info after the current load will be finished.
  bool needs_to_load_handle_info_ = false;
};

class Thread {
 public:
  Thread(Process* process, zx_koid_t koid) : process_(process), koid_(koid) {}

  Process* process() const { return process_; }
  zx_koid_t koid() const { return koid_; }

 private:
  Process* const process_;
  const zx_koid_t koid_;
};

class Event {
 public:
  explicit Event(int64_t timestamp, const Thread* thread, const Syscall* syscall)
      : timestamp_(timestamp), thread_(thread), syscall_(syscall) {}

  // Timestamp in nanoseconds.
  int64_t timestamp() const { return timestamp_; }

  const Thread* thread() const { return thread_; }
  const Syscall* syscall() const { return syscall_; }
  const std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>>&
  inline_fields() const {
    return inline_fields_;
  }
  const std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>>&
  outline_fields() const {
    return outline_fields_;
  }

  void AddInlineField(const fidl_codec::StructMember* member,
                      std::unique_ptr<fidl_codec::Value> value) {
    inline_fields_.emplace(std::make_pair(member, std::move(value)));
  }

  void AddOutlineField(const fidl_codec::StructMember* member,
                       std::unique_ptr<fidl_codec::Value> value) {
    outline_fields_.emplace(std::make_pair(member, std::move(value)));
  }

  // Returns true if we need to load information about the handle (call to zx_object_get_info with
  // ZX_INFO_HANDLE_TABLE). We need to load information about the handle if one of the handles of
  // the event has an unknown koid.
  bool NeedsToLoadHandleInfo(zx_koid_t pid, Inference* inference);

 private:
  int64_t timestamp_;
  const Thread* const thread_;
  const Syscall* const syscall_;
  std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>> inline_fields_;
  std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>> outline_fields_;
};

// Event which represent the arguments of a syscall (When the syscall is called).
class InvokedEvent : public Event {
 public:
  InvokedEvent(int64_t timestamp, const Thread* thread, const Syscall* syscall)
      : Event(timestamp, thread, syscall) {}

  void PrettyPrint(FidlcatPrinter& printer) const;
};

// Event that represents the return value and out parameters when a syscall returns.
class OutputEvent : public Event {
 public:
  OutputEvent(int64_t timestamp, const Thread* thread, const Syscall* syscall,
              int64_t returned_value)
      : Event(timestamp, thread, syscall), returned_value_(returned_value) {}

  void PrettyPrint(FidlcatPrinter& printer) const;

 private:
  int64_t returned_value_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_EVENT_H_
