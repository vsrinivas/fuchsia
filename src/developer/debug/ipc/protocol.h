// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_IPC_PROTOCOL_H_
#define SRC_DEVELOPER_DEBUG_IPC_PROTOCOL_H_

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/arch.h"
#include "src/developer/debug/shared/register_info.h"
#include "src/developer/debug/shared/status.h"

namespace debug_ipc {

constexpr uint32_t kProtocolVersion = 50;

// This is so that it's obvious if the timestamp wasn't properly set (that number should be at
// least 30,000 years) but it's not the max so that if things add to it then time keeps moving
// forward.
const uint64_t kTimestampDefault = 0x0fefffffffffffff;

// The arch values are encoded in the protocol, if these change the protocol version above needs to
// be updated.
static_assert(static_cast<int>(debug::Arch::kX64) == 1);
static_assert(static_cast<int>(debug::Arch::kArm64) == 2);

#pragma pack(push, 8)

// A message consists of a MsgHeader followed by a serialized version of
// whatever struct is associated with that message type. Use the MessageWriter
// class to build this up, which will reserve room for the header and allows
// the structs to be appended, possibly dynamically.
struct MsgHeader {
  enum class Type : uint32_t {
    kNone = 0,
    kHello,

    kAddOrChangeBreakpoint,
    kAddressSpace,
    kAttach,
    kDetach,
    kUpdateFilter,
    kKill,
    kLaunch,
    kModules,
    kPause,
    kProcessTree,
    kReadMemory,
    kReadRegisters,
    kWriteRegisters,
    kRemoveBreakpoint,
    kResume,
    kStatus,
    kSysInfo,
    kThreadStatus,
    kThreads,
    kWriteMemory,
    kLoadInfoHandleTable,
    kUpdateGlobalSettings,
    kSaveMinidump,

    // The "notify" messages are sent unrequested from the agent to the client.
    kNotifyException,
    kNotifyIO,
    kNotifyModules,
    kNotifyProcessExiting,
    kNotifyProcessStarting,
    kNotifyThreadExiting,
    kNotifyThreadStarting,
    kNotifyLog,
    kNotifyComponentExiting,
    kNotifyComponentStarting,

    kNumMessages
  };
  static const char* TypeToString(Type);

  MsgHeader() = default;
  explicit MsgHeader(Type t) : type(t) {}

  uint32_t size = 0;  // Size includes this header.
  Type type = Type::kNone;

  // The transaction ID is assigned by the sender of a request, and is echoed
  // in the reply so the transaction can be easily correlated.
  //
  // Notification messages (sent unsolicited from the agent to the client) have
  // a 0 transaction ID.
  uint32_t transaction_id = 0;

  static constexpr uint32_t kSerializedHeaderSize = sizeof(uint32_t) * 3;
};

struct HelloRequest {};
struct HelloReply {
  // Stream signature to make sure we're talking to the right service.
  // This number is ASCII for "zxdbIPC>".
  static constexpr uint64_t kStreamSignature = 0x7a7864624950433e;

  uint64_t signature = kStreamSignature;
  uint32_t version = kProtocolVersion;
  debug::Arch arch = debug::Arch::kUnknown;
  uint64_t page_size = 0;
};

enum class InferiorType : uint32_t {
  kBinary,
  kComponent,
  kTest,
  kLast,
};
const char* InferiorTypeToString(InferiorType);

// Status ------------------------------------------------------------------------------------------
//
// Asks for a present view of the system.

struct StatusRequest {};
struct StatusReply {
  // All the processes that the debug agent is currently attached.
  std::vector<ProcessRecord> processes;

  // List of processes waiting on limbo. Limbo are the processes that triggered an exception with
  // no exception handler attached. If the system is configured to keep those around in order to
  // wait for a debugger, it is said that those processes are in "limbo".
  std::vector<ProcessRecord> limbo;
};

struct LaunchRequest {
  InferiorType inferior_type = InferiorType::kLast;

  // argv[0] is the app to launch.
  std::vector<std::string> argv;
};
struct LaunchReply {
  uint64_t timestamp = kTimestampDefault;

  // Result of launch.
  debug::Status status;

  // process_id and process_name are only valid when inferior_type is kBinary.
  uint64_t process_id = 0;
  std::string process_name;
};

struct KillRequest {
  uint64_t process_koid = 0;
};
struct KillReply {
  uint64_t timestamp = kTimestampDefault;
  debug::Status status;
};

// The debug agent will follow a successful AttachReply with notifications for
// all threads currently existing in the attached process.
struct AttachRequest {
  uint64_t koid = 0;
};

struct AttachReply {
  uint64_t timestamp = kTimestampDefault;
  uint64_t koid = 0;
  debug::Status status;  // Result of attaching.
  std::string name;

  // The component information if the task is a process and the process is running in a component.
  std::optional<ComponentInfo> component;
};

struct DetachRequest {
  uint64_t koid = 0;
};
struct DetachReply {
  uint64_t timestamp = kTimestampDefault;
  debug::Status status;
};

struct PauseRequest {
  // When empty, pauses all threads in all processes. An entry with a process koid and a 0 thread
  // koid will resume all threads of the given process.
  std::vector<ProcessThreadId> ids;
};
// The backend should make a best effort to ensure the requested threads are actually stopped before
// sending the reply.
struct PauseReply {
  // The updated thead state for all affected threads.
  std::vector<ThreadRecord> threads;
};

struct ResumeRequest {
  enum class How : uint32_t {
    kResolveAndContinue = 0,  // Marks the exception as handled and continues executions.
    kForwardAndContinue,      // Marks the exception as second-chance and continues executions.
    kStepInstruction,         // Step |count| machine instructions.
    kStepInRange,             // Step until control exits an address range.

    kLast  // Not a real state, used for validation.
  };

  // Whether a give resume mode steps.
  static bool MakesStep(How how) {
    return (how == How::kStepInstruction || how == How::kStepInRange);
  }
  static const char* HowToString(How);

  // If empty, all threads of all debugged processes will be continued. An entry with a process
  // koid and a 0 thread koid will resume all threads of the given process.
  //
  // kStepInRange may only be used with a unique thread.
  std::vector<ProcessThreadId> ids;

  How how = How::kResolveAndContinue;

  // When how == kStepInstruction, how many instructions to step.
  uint64_t count = 1;

  // When how == kStepInRange, these variables define the address range to step in. As long as the
  // instruction pointer is inside [range_begin, range_end), execution will continue.
  uint64_t range_begin = 0;
  uint64_t range_end = 0;
};
struct ResumeReply {};

struct ProcessTreeRequest {};
struct ProcessTreeReply {
  ProcessTreeRecord root;
};

struct ThreadsRequest {
  uint64_t process_koid = 0;
};
struct ThreadsReply {
  // If there is no such process, the threads array will be empty.
  std::vector<ThreadRecord> threads;
};

struct ReadMemoryRequest {
  uint64_t process_koid = 0;
  uint64_t address = 0;
  uint32_t size = 0;
};
struct ReadMemoryReply {
  std::vector<MemoryBlock> blocks;
};

struct AddOrChangeBreakpointRequest {
  BreakpointSettings breakpoint;
};
struct AddOrChangeBreakpointReply {
  // A variety of race conditions could cause a breakpoint modification or set to fail. For example,
  // updating or setting a breakpoint could race with the library containing that code unloading.
  //
  // The update or set will always apply the breakpoint to any contexts that it can apply to (if
  // there are multiple locations, we don't want to remove them all just because one failed).
  // Therefore, you can't definitively say the breakpoint is invalid just because it has a failure
  // code here. If necessary, we can add more information in the failure.
  debug::Status status;
};

struct RemoveBreakpointRequest {
  uint32_t breakpoint_id = 0;
};
struct RemoveBreakpointReply {};

struct SysInfoRequest {};
struct SysInfoReply {
  std::string version;
  uint32_t num_cpus;
  uint32_t memory_mb;
  uint32_t hw_breakpoint_count;
  uint32_t hw_watchpoint_count;
};

// The thread state request asks for the current thread status with a full
// backtrace if it is suspended. If the thread with the given KOID doesn't
// exist, the ThreadRecord will report a "kDead" status.
struct ThreadStatusRequest {
  ProcessThreadId id;
};
struct ThreadStatusReply {
  ThreadRecord record;
};

struct AddressSpaceRequest {
  uint64_t process_koid = 0;
  // if non-zero |address| indicates to return only the regions
  // that contain it.
  uint64_t address = 0;
};

struct AddressSpaceReply {
  std::vector<AddressRegion> map;
};

struct ModulesRequest {
  uint64_t process_koid = 0;
};
struct ModulesReply {
  std::vector<Module> modules;
};

// Request to set filter.
struct UpdateFilterRequest {
  std::vector<Filter> filters;
};

struct UpdateFilterReply {
  // List of koids for currently running processes that match any of the filters.
  // Guaranteed that each koid is unique.
  std::vector<uint64_t> matched_processes;
};

struct WriteMemoryRequest {
  uint64_t process_koid = 0;
  uint64_t address = 0;
  std::vector<uint8_t> data;
};

struct WriteMemoryReply {
  debug::Status status;
};

struct LoadInfoHandleTableRequest {
  uint64_t process_koid = 0;
};
struct LoadInfoHandleTableReply {
  debug::Status status;
  std::vector<InfoHandle> handles;
};

struct UpdateGlobalSettingsRequest {
  // Updates how the default strategy for handling a particular exception type.
  struct UpdateExceptionStrategy {
    ExceptionType type = ExceptionType::kNone;
    ExceptionStrategy value = ExceptionStrategy::kNone;
  };

  std::vector<UpdateExceptionStrategy> exception_strategies;
};

struct UpdateGlobalSettingsReply {
  debug::Status status;
};

struct SaveMinidumpRequest {
  uint64_t process_koid = 0;
};

struct SaveMinidumpReply {
  debug::Status status;
  std::vector<uint8_t> core_data;
};

// ReadRegisters ---------------------------------------------------------------

struct ReadRegistersRequest {
  ProcessThreadId id;

  // What categories do we want to receive data from.
  std::vector<debug::RegisterCategory> categories;
};

struct ReadRegistersReply {
  std::vector<debug::RegisterValue> registers;
};

// WriteRegisters --------------------------------------------------------------

struct WriteRegistersRequest {
  ProcessThreadId id;
  std::vector<debug::RegisterValue> registers;
};

struct WriteRegistersReply {
  debug::Status status;

  // The latest registers from all affected categories after the write.
  //
  // This allows clients to validate that the change actually took effect. All known registers
  // from all categories changed by the write request will be sent.
  std::vector<debug::RegisterValue> registers;
};

// Notifications ---------------------------------------------------------------

// Notify that a new process of interest was created and attached.

struct NotifyProcessStarting {
  uint64_t timestamp = kTimestampDefault;
  enum class Type : uint32_t {
    kAttach,  // The process was attached from a filter.
    kLaunch,  // The process was attached from a component launching.
    kLimbo,   // The process entered the limbo and is NOT attached.

    kLast,  // Not valid, for verification purposes.
  };
  static const char* TypeToString(Type);

  Type type = Type::kAttach;

  uint64_t koid = 0;
  std::string name;

  // The component information if the process is running in a component.
  std::optional<ComponentInfo> component;
};

// Data for process destroyed messages (process created messages are in
// response to launch commands so is just the reply to that message).
struct NotifyProcessExiting {
  uint64_t timestamp = kTimestampDefault;
  uint64_t process_koid = 0;
  int64_t return_code = 0;
};

// Data for thread created and destroyed messages.
struct NotifyThread {
  uint64_t timestamp = kTimestampDefault;
  ThreadRecord record;
};

// Data passed for exceptions.
struct NotifyException {
  uint64_t timestamp = kTimestampDefault;
  // Holds the state and a minimal stack (up to 2 frames) of the thread at the
  // moment of notification.
  ThreadRecord thread;

  ExceptionType type = ExceptionType::kNone;

  ExceptionRecord exception;

  // When the stop was caused by hitting a breakpoint, this vector will contain
  // the post-hit stats of every hit breakpoint (since there can be more than
  // one breakpoint at any given address).
  //
  // Be sure to check should_delete on each of these and update local state as
  // necessary.
  std::vector<BreakpointStats> hit_breakpoints;

  // Lists all other threads affected by this exception. Breakpoints can indicate that other threads
  // in the same process or all attached processes should be stopped when the breakpoint is hit.
  // This vector will not contain the thread that was stopped (the "thread" member above), and it
  // will not contain threads that were already stopped at the time of the exception.
  std::vector<ThreadRecord> other_affected_threads;

  // If automation was requested, then this contains the memory requested
  // Otherwise this is just an empty vector.
  std::vector<MemoryBlock> memory_blocks;
};

// Indicates the loaded modules may have changed. The entire list of current
// modules is sent every time.
struct NotifyModules {
  uint64_t timestamp = kTimestampDefault;
  uint64_t process_koid = 0;
  std::vector<Module> modules;

  // The list of threads in the process stopped automatically as a result of the module load. The
  // client will want to resume these threads once it has processed the load.
  std::vector<ProcessThreadId> stopped_threads;
};

struct NotifyIO {
  uint64_t timestamp = kTimestampDefault;
  static constexpr size_t kMaxDataSize = 64 * 1024;  // 64k.

  enum class Type : uint32_t {
    kStderr,
    kStdout,
    kLast,  // Not a real type.
  };
  static const char* TypeToString(Type);

  uint64_t process_koid = 0;  // Could be 0 if the output is not from an attached process.
  Type type = Type::kLast;

  // Data will be up most kMaxDataSize bytes.
  std::string data;

  // Whether this is a piece of bigger message.
  // This information can be used by the consumer to change how it will handle
  // this data.
  bool more_data_available = false;
};

// Redirects a log message in debug_agent to the frontend, serving two purposes:
//   1) Forwards important warnings or errors that the end users would rather know.
//   2) Forwards info and debug logs for debugger developers, when the debug mode is turned on.
struct NotifyLog {
  uint64_t timestamp = kTimestampDefault;

  enum class Severity : uint32_t {
    kDebug,  // Not used for now.
    kInfo,   // Not used for now.
    kWarn,
    kError,
    kLast,  // Not a real level.
  };

  struct Location {
    std::string file;
    std::string function;
    uint32_t line = 0;
  };

  Severity severity = Severity::kInfo;
  Location location;
  std::string log;
};

// Notify that a component has started or exited.
// Used by both |kNotifyComponentExiting| and |kNotifyComponentStarting|.
// Only components of interest, i.e., those matching at least one of the filters, will be notified.
struct NotifyComponent {
  uint64_t timestamp = kTimestampDefault;

  ComponentInfo component;
};

#pragma pack(pop)

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_IPC_PROTOCOL_H_
