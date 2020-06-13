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
#include "tools/fidlcat/lib/fidlcat_printer.h"

namespace fidlcat {

class Location;
class Syscall;
class SyscallDecoder;

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

// Defines a location in the source (used by stack frames).
class Location {
 public:
  Location(const std::string& path, uint32_t line, uint32_t column, uint64_t address,
           const std::string& symbol)
      : path_(path), line_(line), column_(column), address_(address), symbol_(symbol) {}

  const std::string& path() const { return path_; }
  uint32_t line() const { return line_; }
  uint32_t column() const { return column_; }
  uint64_t address() const { return address_; }
  const std::string& symbol() const { return symbol_; }

 private:
  const std::string path_;
  const uint32_t line_;
  const uint32_t column_;
  const uint64_t address_;
  const std::string symbol_;
};

class Event {
 public:
  Event(int64_t timestamp, Thread* thread) : timestamp_(timestamp), thread_(thread) {}

  // Timestamp in nanoseconds.
  int64_t timestamp() const { return timestamp_; }

  Thread* thread() const { return thread_; }

 private:
  const int64_t timestamp_;
  Thread* const thread_;
};

// Base class for events related to a syscall.
class SyscallEvent : public Event {
 public:
  SyscallEvent(int64_t timestamp, Thread* thread, const Syscall* syscall)
      : Event(timestamp, thread), syscall_(syscall) {}

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

  const fidl_codec::FidlMessageValue* GetMessage() const;

 private:
  const Syscall* const syscall_;
  std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>> inline_fields_;
  std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>> outline_fields_;
};

// Event that represents the arguments of a syscall (When the syscall is called).
class InvokedEvent : public SyscallEvent {
 public:
  InvokedEvent(int64_t timestamp, Thread* thread, const Syscall* syscall)
      : SyscallEvent(timestamp, thread, syscall) {}

  const std::vector<Location>& stack_frame() const { return stack_frame_; }
  std::vector<Location>& stack_frame() { return stack_frame_; }

  void PrettyPrint(FidlcatPrinter& printer) const;

 public:
  std::vector<Location> stack_frame_;
};

// Event that represents the return value and out parameters when a syscall returns.
class OutputEvent : public SyscallEvent {
 public:
  OutputEvent(int64_t timestamp, Thread* thread, const Syscall* syscall, int64_t returned_value)
      : SyscallEvent(timestamp, thread, syscall), returned_value_(returned_value) {}

  void PrettyPrint(FidlcatPrinter& printer) const;

 private:
  int64_t returned_value_;
};

// Event that represents an exception.
class ExceptionEvent : public Event {
 public:
  ExceptionEvent(int64_t timestamp, Thread* thread) : Event(timestamp, thread) {}

  const std::vector<Location>& stack_frame() const { return stack_frame_; }
  std::vector<Location>& stack_frame() { return stack_frame_; }

  void PrettyPrint(FidlcatPrinter& printer) const;

 private:
  std::vector<Location> stack_frame_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_EVENT_H_
