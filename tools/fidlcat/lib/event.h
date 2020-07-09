// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_EVENT_H_
#define TOOLS_FIDLCAT_LIB_EVENT_H_

#include <zircon/system/public/zircon/types.h>

#include <map>
#include <memory>
#include <vector>

#include "src/developer/debug/zxdb/client/process.h"
#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fidl_codec/wire_types.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "tools/fidlcat/lib/fidlcat_printer.h"
#include "tools/fidlcat/proto/session.pb.h"

namespace fidlcat {

class Event;
class Location;
class Syscall;
class SyscallDecoder;
class SyscallDisplayDispatcher;
class Thread;

class Handle {
 public:
  Handle(Thread* thread, uint32_t handle, int64_t creation_time, bool startup)
      : thread_(thread), handle_(handle), creation_time_(creation_time), startup_(startup) {}

  Thread* thread() const { return thread_; }
  uint32_t handle() const { return handle_; }
  int64_t creation_time() const { return creation_time_; }
  bool startup() const { return startup_; }

 private:
  Thread* const thread_;
  const uint32_t handle_;
  const int64_t creation_time_;
  const bool startup_;
};

class Process {
 public:
  Process(std::string_view name, zx_koid_t koid, fxl::WeakPtr<zxdb::Process> zxdb_process)
      : name_(name), koid_(koid), zxdb_process_(zxdb_process) {}

  const std::string& name() const { return name_; }
  zx_koid_t koid() const { return koid_; }
  zxdb::Process* zxdb_process() const { return zxdb_process_.get(); }
  std::map<uint32_t, Handle*>& handle_map() { return handle_map_; }

  void LoadHandleInfo(Inference* inference);

  Handle* SearchHandle(uint32_t handle) const {
    auto result = handle_map_.find(handle);
    if (result == handle_map_.end()) {
      return nullptr;
    }
    return result->second;
  }

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
  std::map<uint32_t, Handle*> handle_map_;
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
  explicit Event(int64_t timestamp) : timestamp_(timestamp) {}
  virtual ~Event() = default;

  // Timestamp in nanoseconds.
  int64_t timestamp() const { return timestamp_; }

  // Write the content of the event into a protobuf event.
  virtual void Write(proto::Event* dst) const = 0;

 private:
  const int64_t timestamp_;
};

// Event which gives the result of a process launching.
class ProcessLaunchedEvent final : public Event {
 public:
  ProcessLaunchedEvent(int64_t timestamp, std::string_view command, std::string_view error_message)
      : Event(timestamp), command_(command), error_message_(error_message) {}

  const std::string& command() const { return command_; }
  const std::string& error_message() const { return error_message_; }

  void Write(proto::Event* dst) const override;

 private:
  const std::string command_;
  const std::string error_message_;
};

// Event which tells that we started monitoring a process.
class ProcessMonitoredEvent final : public Event {
 public:
  ProcessMonitoredEvent(int64_t timestamp, Process* process, std::string_view error_message)
      : Event(timestamp), process_(process), error_message_(error_message) {}

  Process* process() const { return process_; }
  const std::string& error_message() const { return error_message_; }

  void Write(proto::Event* dst) const override;

 private:
  Process* const process_;
  const std::string error_message_;
};

// Event which tells that we stop monitoring a process.
class StopMonitoringEvent final : public Event {
 public:
  StopMonitoringEvent(int64_t timestamp, Process* process) : Event(timestamp), process_(process) {}

  Process* process() const { return process_; }

  void Write(proto::Event* dst) const override;

 private:
  Process* const process_;
};

// Base classe for all events related to a thread.
class ThreadEvent : public Event {
 public:
  ThreadEvent(int64_t timestamp, Thread* thread) : Event(timestamp), thread_(thread) {}

  Thread* thread() const { return thread_; }

 private:
  Thread* const thread_;
};

// Base class for events related to a syscall.
class SyscallEvent : public ThreadEvent {
 public:
  SyscallEvent(int64_t timestamp, Thread* thread, const Syscall* syscall)
      : ThreadEvent(timestamp, thread), syscall_(syscall) {}

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

  const fidl_codec::HandleValue* GetHandleValue(const fidl_codec::StructMember* member) const;

 private:
  const Syscall* const syscall_;
  std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>> inline_fields_;
  std::map<const fidl_codec::StructMember*, std::unique_ptr<fidl_codec::Value>> outline_fields_;
};

// Event that represents the arguments of a syscall (When the syscall is called).
class InvokedEvent final : public SyscallEvent {
 public:
  InvokedEvent(int64_t timestamp, Thread* thread, const Syscall* syscall)
      : SyscallEvent(timestamp, thread, syscall) {}

  uint32_t id() const { return id_; }
  void set_id(uint32_t id) { id_ = id; }

  const std::vector<Location>& stack_frame() const { return stack_frame_; }
  std::vector<Location>& stack_frame() { return stack_frame_; }

  bool displayed() const { return displayed_; }
  void set_displayed() { displayed_ = true; }

  void Write(proto::Event* dst) const override;

  void PrettyPrint(FidlcatPrinter& printer) const;

 public:
  uint32_t id_ = 0;
  std::vector<Location> stack_frame_;
  bool displayed_ = false;
};

// Event that represents the return value and out parameters when a syscall returns.
class OutputEvent final : public SyscallEvent {
 public:
  OutputEvent(int64_t timestamp, Thread* thread, const Syscall* syscall, int64_t returned_value,
              std::shared_ptr<InvokedEvent> invoked_event)
      : SyscallEvent(timestamp, thread, syscall),
        returned_value_(returned_value),
        invoked_event_(std::move(invoked_event)) {}

  int64_t returned_value() const { return returned_value_; }
  const InvokedEvent* invoked_event() const { return invoked_event_.get(); }

  void Write(proto::Event* dst) const override;

  void PrettyPrint(FidlcatPrinter& printer) const;

 private:
  const int64_t returned_value_;
  // The event which describes the input arguments for this syscall output event.
  std::shared_ptr<InvokedEvent> invoked_event_;
};

// Event that represents an exception.
class ExceptionEvent final : public ThreadEvent {
 public:
  ExceptionEvent(int64_t timestamp, Thread* thread) : ThreadEvent(timestamp, thread) {}

  const std::vector<Location>& stack_frame() const { return stack_frame_; }
  std::vector<Location>& stack_frame() { return stack_frame_; }

  void Write(proto::Event* dst) const override;

  void PrettyPrint(FidlcatPrinter& printer) const;

 private:
  std::vector<Location> stack_frame_;
};

// Class to decode events from protobuf.
class EventDecoder {
 public:
  explicit EventDecoder(SyscallDisplayDispatcher* dispatcher) : dispatcher_(dispatcher) {}

  SyscallDisplayDispatcher* dispatcher() const { return dispatcher_; }

  // Decodes a protobuf event and dispatch it.
  bool DecodeAndDispatchEvent(const proto::Event& proto_event);

 private:
  // Decode the values for a syscall event.
  bool DecodeValues(
      SyscallEvent* event,
      const ::google::protobuf::Map<::std::string, ::fidl_codec::proto::Value>& inline_fields,
      const ::google::protobuf::Map<uint32_t, ::fidl_codec::proto::Value>& inline_id_fields,
      const ::google::protobuf::Map<::std::string, ::fidl_codec::proto::Value>& outline_fields,
      const ::google::protobuf::Map<uint32_t, ::fidl_codec::proto::Value>& outline_id_fields,
      bool invoked);

  // Dispatcher used to decode the events.
  SyscallDisplayDispatcher* dispatcher_;

  // Map of all invoked events already decoded. Used to associate the invoked event to an output
  // event.
  std::map<uint32_t, std::shared_ptr<InvokedEvent>> invoked_events_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_EVENT_H_
