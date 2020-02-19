// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_EVENT_H_
#define TOOLS_FIDLCAT_LIB_EVENT_H_

#include <zircon/system/public/zircon/types.h>

#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fidl_codec/wire_types.h"
#include "tools/fidlcat/lib/type_decoder.h"

namespace fidlcat {

class Syscall;
class SyscallDecoder;

// Printer which allows us to print the infered data for handles.
class FidlcatPrinter : public fidl_codec::PrettyPrinter {
 public:
  FidlcatPrinter(SyscallDecoder* decoder, std::ostream& os, const fidl_codec::Colors& colors,
                 std::string_view line_header, int max_line_size, bool header_on_every_line,
                 int tabulations = 0)
      : PrettyPrinter(os, colors, line_header, max_line_size, header_on_every_line, tabulations),
        decoder_(decoder) {}

  void DisplayHandle(const zx_handle_info_t& handle) override;
  void DisplayStatus(zx_status_t status);
  void DisplayTime(zx_time_t time_ns);
  bool DisplayReturnedValue(SyscallReturnType type, int64_t returned_value);
  void DisplayInline(
      const std::vector<std::unique_ptr<fidl_codec::StructMember>>& members,
      const std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>>& values);

 private:
  SyscallDecoder* decoder_;
};

class Process {
 public:
  Process(std::string_view name, zx_koid_t koid) : name_(name), koid_(koid) {}

  const std::string& name() const { return name_; }
  zx_koid_t koid() const { return koid_; }

 private:
  const std::string name_;
  const zx_koid_t koid_;
};

class Thread {
 public:
  Thread(const Process* process, zx_koid_t koid) : process_(process), koid_(koid) {}

  const Process* process() const { return process_; }
  zx_koid_t koid() const { return koid_; }

 private:
  const Process* const process_;
  const zx_koid_t koid_;
};

class Event {
 public:
  explicit Event(int64_t timestamp) : timestamp_(timestamp) {}

  // Timestamp in nanoseconds.
  int64_t timestamp() const { return timestamp_; }

 private:
  int64_t timestamp_;
};

// Event which represent the arguments of a syscall (When the syscall is called).
class InvokedEvent : public Event {
 public:
  InvokedEvent(int64_t timestamp, const Thread* thread, const Syscall* syscall)
      : Event(timestamp), thread_(thread), syscall_(syscall) {}

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
    inline_fields_.emplace(std::make_pair(member, std::move(value)));
  }

  void PrettyPrint(FidlcatPrinter& printer);

 private:
  const Thread* const thread_;
  const Syscall* const syscall_;
  std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>> inline_fields_;
  std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>> outline_fields_;
};

// Event that represents the return value and out parameters when a syscall returns.
class OutputEvent : public Event {
 public:
  OutputEvent(int64_t timestamp, const Thread* thread, const Syscall* syscall,
              int64_t returned_value)
      : Event(timestamp), thread_(thread), syscall_(syscall), returned_value_(returned_value) {}

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
    inline_fields_.emplace(std::make_pair(member, std::move(value)));
  }

  void PrettyPrint(FidlcatPrinter& printer);

 private:
  const Thread* const thread_;
  const Syscall* const syscall_;
  int64_t returned_value_;
  std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>> inline_fields_;
  std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>> outline_fields_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_EVENT_H_
