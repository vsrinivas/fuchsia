// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_IPC_PROTOCOL_H_
#define SRC_DEVELOPER_DEBUG_IPC_PROTOCOL_H_

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/arch.h"
#include "src/developer/debug/shared/register_info.h"
#include "src/developer/debug/shared/serialization.h"
#include "src/developer/debug/shared/status.h"

namespace debug_ipc {

// ABI Compatibility Guide
//
// Goal: within the same Fuchsia API level, different versions of zxdb and debug_agent should be
// compatible with each other.
//
//   - If you want to rename something, don't bump the version number because ABI doesn't change.
//   - If you want to add/remove a field to/from a message, bump the version number, and use e.g.
//     `if (ver > ...) { ser | some_field; }` in the `Serialize` function.
//   - If you want to add a new request/notification type, pick a new message id, bump the version,
//     and define a static variable `uint32_t kSupportedSinceVersion` in the definition of the
//     request/notification type. This will make the `Serialize` function return empty bytes when
//     serializing so the message won't be sent.
//   - You don't want to remove a message type. Just mark it as deprecated but still handle it
//     when receiving it.
//   - More complex logic could be implemented by checking the protocol version before sending.
//   - kMinimumProtocolVersion can only be updated when FUCHSIA_API_LEVEL bumps, which means we
//     should increase kMinimumProtocolVersion to the kCurrentProtocolVersion and the support for
//     old versions can be dropped.

constexpr uint32_t kCurrentProtocolVersion = 52;

#if !defined(FUCHSIA_API_LEVEL)
#error FUCHSIA_API_LEVEL must be defined
#elif FUCHSIA_API_LEVEL == 9
constexpr uint32_t kMinimumProtocolVersion = 52;
#else
constexpr uint32_t kMinimumProtocolVersion = kCurrentProtocolVersion;
#endif

// This is so that it's obvious if the timestamp wasn't properly set (that number should be at
// least 30,000 years) but it's not the max so that if things add to it then time keeps moving
// forward.
const uint64_t kTimestampDefault = 0x0fefffffffffffff;

// The arch values are encoded in the protocol, if these change the protocol version above needs to
// be updated.
static_assert(static_cast<int>(debug::Arch::kX64) == 1);
static_assert(static_cast<int>(debug::Arch::kArm64) == 2);

#pragma pack(push, 8)

// Enumerate over the name of all possible request/reply types. The message id will be
// MsgHeader::Type::k##name, the request type is name##Request, and the reply type is name##Reply.
#define FOR_EACH_REQUEST_TYPE(FN) \
  FN(Hello)                       \
  FN(AddOrChangeBreakpoint)       \
  FN(AddressSpace)                \
  FN(Attach)                      \
  FN(Detach)                      \
  FN(UpdateFilter)                \
  FN(Kill)                        \
  FN(Launch)                      \
  FN(Modules)                     \
  FN(Pause)                       \
  FN(ProcessTree)                 \
  FN(ReadMemory)                  \
  FN(ReadRegisters)               \
  FN(WriteRegisters)              \
  FN(RemoveBreakpoint)            \
  FN(Resume)                      \
  FN(Status)                      \
  FN(SysInfo)                     \
  FN(ThreadStatus)                \
  FN(Threads)                     \
  FN(WriteMemory)                 \
  FN(LoadInfoHandleTable)         \
  FN(UpdateGlobalSettings)        \
  FN(SaveMinidump)

// The "notify" messages are sent unrequested from the agent to the client.
//
// Enumerate over the name of all possible notification types. The message id is
// MsgHeader::Type::k##name, and the type is type.
#define FOR_EACH_NOTIFICATION_TYPE(FN) \
  FN(NotifyException)                  \
  FN(NotifyIO)                         \
  FN(NotifyModules)                    \
  FN(NotifyProcessExiting)             \
  FN(NotifyProcessStarting)            \
  FN(NotifyThreadExiting)              \
  FN(NotifyThreadStarting)             \
  FN(NotifyLog)                        \
  FN(NotifyComponentExiting)           \
  FN(NotifyComponentStarting)

// A message consists of a MsgHeader followed by a serialized version of
// whatever struct is associated with that message type. Use the MessageWriter
// class to build this up, which will reserve room for the header and allows
// the structs to be appended, possibly dynamically.
struct MsgHeader {
  enum class Type : uint32_t {
    kNone = 0,

    kHello = 1,
    kAddOrChangeBreakpoint = 2,
    kAddressSpace = 3,
    kAttach = 4,
    kDetach = 5,
    kUpdateFilter = 6,
    kKill = 7,
    kLaunch = 8,
    kModules = 9,
    kPause = 10,
    kProcessTree = 11,
    kReadMemory = 12,
    kReadRegisters = 13,
    kWriteRegisters = 14,
    kRemoveBreakpoint = 15,
    kResume = 16,
    kStatus = 17,
    kSysInfo = 18,
    kThreadStatus = 19,
    kThreads = 20,
    kWriteMemory = 21,
    kLoadInfoHandleTable = 22,
    kUpdateGlobalSettings = 23,
    kSaveMinidump = 24,

    kNotifyException = 101,
    kNotifyIO = 102,
    kNotifyModules = 103,
    kNotifyProcessExiting = 104,
    kNotifyProcessStarting = 105,
    kNotifyThreadExiting = 106,
    kNotifyThreadStarting = 107,
    kNotifyLog = 108,
    kNotifyComponentExiting = 109,
    kNotifyComponentStarting = 110,
  };
  static const char* TypeToString(Type);

  uint32_t size = 0;  // Size includes this header.
  Type type = Type::kNone;

  // The transaction ID is assigned by the sender of a request, and is echoed
  // in the reply so the transaction can be easily correlated.
  //
  // Notification messages (sent unsolicited from the agent to the client) have
  // a 0 transaction ID.
  uint32_t transaction_id = 0;

  static constexpr uint32_t kSerializedHeaderSize = sizeof(uint32_t) * 3;

  void Serialize(Serializer& ser, uint32_t ver) { ser | size | type | transaction_id; }
};

struct HelloRequest {
  uint32_t version = 0;

  void Serialize(Serializer& ser, uint32_t ver) { ser | version; }
};
struct HelloReply {
  // Stream signature to make sure we're talking to the right service.
  // This number is ASCII for "zxdbIPC>".
  static constexpr uint64_t kStreamSignature = 0x7a7864624950433e;

  uint64_t signature = kStreamSignature;
  uint32_t version = 0;
  debug::Arch arch = debug::Arch::kUnknown;
  uint64_t page_size = 0;

  void Serialize(Serializer& ser, uint32_t ver) { ser | signature | version | arch | page_size; }
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

struct StatusRequest {
  void Serialize(Serializer& ser, uint32_t ver) {}
};
struct StatusReply {
  // All the processes that the debug agent is currently attached.
  std::vector<ProcessRecord> processes;

  // List of processes waiting on limbo. Limbo are the processes that triggered an exception with
  // no exception handler attached. If the system is configured to keep those around in order to
  // wait for a debugger, it is said that those processes are in "limbo".
  std::vector<ProcessRecord> limbo;

  void Serialize(Serializer& ser, uint32_t ver) { ser | processes | limbo; }
};

struct LaunchRequest {
  InferiorType inferior_type = InferiorType::kLast;

  // argv[0] is the app to launch.
  std::vector<std::string> argv;

  void Serialize(Serializer& ser, uint32_t ver) { ser | inferior_type | argv; }
};
struct LaunchReply {
  uint64_t timestamp = kTimestampDefault;

  // Result of launch.
  debug::Status status;

  // process_id and process_name are only valid when inferior_type is kBinary.
  uint64_t process_id = 0;
  std::string process_name;

  void Serialize(Serializer& ser, uint32_t ver) {
    ser | timestamp | status | process_id | process_name;
  }
};

struct KillRequest {
  uint64_t process_koid = 0;

  void Serialize(Serializer& ser, uint32_t ver) { ser | process_koid; }
};
struct KillReply {
  uint64_t timestamp = kTimestampDefault;
  debug::Status status;

  void Serialize(Serializer& ser, uint32_t ver) { ser | timestamp | status; }
};

// The debug agent will follow a successful AttachReply with notifications for
// all threads currently existing in the attached process.
struct AttachRequest {
  uint64_t koid = 0;

  void Serialize(Serializer& ser, uint32_t ver) { ser | koid; }
};

struct AttachReply {
  uint64_t timestamp = kTimestampDefault;
  uint64_t koid = 0;
  debug::Status status;  // Result of attaching.
  std::string name;

  // The component information if the task is a process and the process is running in a component.
  std::optional<ComponentInfo> component;

  void Serialize(Serializer& ser, uint32_t ver) {
    ser | timestamp | koid | status | name | component;
  }
};

struct DetachRequest {
  uint64_t koid = 0;

  void Serialize(Serializer& ser, uint32_t ver) { ser | koid; }
};
struct DetachReply {
  uint64_t timestamp = kTimestampDefault;
  debug::Status status;

  void Serialize(Serializer& ser, uint32_t ver) { ser | timestamp | status; }
};

struct PauseRequest {
  // When empty, pauses all threads in all processes. An entry with a process koid and a 0 thread
  // koid will resume all threads of the given process.
  std::vector<ProcessThreadId> ids;

  void Serialize(Serializer& ser, uint32_t ver) { ser | ids; }
};
// The backend should make a best effort to ensure the requested threads are actually stopped before
// sending the reply.
struct PauseReply {
  // The updated thead state for all affected threads.
  std::vector<ThreadRecord> threads;

  void Serialize(Serializer& ser, uint32_t ver) { ser | threads; }
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

  void Serialize(Serializer& ser, uint32_t ver) {
    ser | ids | how | count | range_begin | range_end;
  }
};
struct ResumeReply {
  void Serialize(Serializer& ser, uint32_t ver) {}
};

struct ProcessTreeRequest {
  void Serialize(Serializer& ser, uint32_t ver) {}
};

struct ProcessTreeReply {
  ProcessTreeRecord root;

  void Serialize(Serializer& ser, uint32_t ver) { ser | root; }
};

struct ThreadsRequest {
  uint64_t process_koid = 0;

  void Serialize(Serializer& ser, uint32_t ver) { ser | process_koid; }
};
struct ThreadsReply {
  // If there is no such process, the threads array will be empty.
  std::vector<ThreadRecord> threads;

  void Serialize(Serializer& ser, uint32_t ver) { ser | threads; }
};

struct ReadMemoryRequest {
  uint64_t process_koid = 0;
  uint64_t address = 0;
  uint32_t size = 0;

  void Serialize(Serializer& ser, uint32_t ver) { ser | process_koid | address | size; }
};
struct ReadMemoryReply {
  std::vector<MemoryBlock> blocks;

  void Serialize(Serializer& ser, uint32_t ver) { ser | blocks; }
};

struct AddOrChangeBreakpointRequest {
  BreakpointSettings breakpoint;

  void Serialize(Serializer& ser, uint32_t ver) { ser | breakpoint; }
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

  void Serialize(Serializer& ser, uint32_t ver) { ser | status; }
};

struct RemoveBreakpointRequest {
  uint32_t breakpoint_id = 0;

  void Serialize(Serializer& ser, uint32_t ver) { ser | breakpoint_id; }
};
struct RemoveBreakpointReply {
  void Serialize(Serializer& ser, uint32_t ver) {}
};

struct SysInfoRequest {
  void Serialize(Serializer& ser, uint32_t ver) {}
};
struct SysInfoReply {
  std::string version;
  uint32_t num_cpus;
  uint32_t memory_mb;
  uint32_t hw_breakpoint_count;
  uint32_t hw_watchpoint_count;

  void Serialize(Serializer& ser, uint32_t ver) {
    ser | version | num_cpus | memory_mb | hw_breakpoint_count | hw_watchpoint_count;
  }
};

// The thread state request asks for the current thread status with a full
// backtrace if it is suspended. If the thread with the given KOID doesn't
// exist, the ThreadRecord will report a "kDead" status.
struct ThreadStatusRequest {
  ProcessThreadId id;

  void Serialize(Serializer& ser, uint32_t ver) { ser | id; }
};
struct ThreadStatusReply {
  ThreadRecord record;

  void Serialize(Serializer& ser, uint32_t ver) { ser | record; }
};

struct AddressSpaceRequest {
  uint64_t process_koid = 0;
  // if non-zero |address| indicates to return only the regions
  // that contain it.
  uint64_t address = 0;

  void Serialize(Serializer& ser, uint32_t ver) { ser | process_koid | address; }
};

struct AddressSpaceReply {
  std::vector<AddressRegion> map;

  void Serialize(Serializer& ser, uint32_t ver) { ser | map; }
};

struct ModulesRequest {
  uint64_t process_koid = 0;

  void Serialize(Serializer& ser, uint32_t ver) { ser | process_koid; }
};
struct ModulesReply {
  std::vector<Module> modules;

  void Serialize(Serializer& ser, uint32_t ver) { ser | modules; }
};

// Request to set filter.
struct UpdateFilterRequest {
  std::vector<Filter> filters;

  void Serialize(Serializer& ser, uint32_t ver) { ser | filters; }
};

struct UpdateFilterReply {
  // List of koids for currently running processes that match any of the filters.
  // Guaranteed that each koid is unique.
  std::vector<uint64_t> matched_processes;

  void Serialize(Serializer& ser, uint32_t ver) { ser | matched_processes; }
};

struct WriteMemoryRequest {
  uint64_t process_koid = 0;
  uint64_t address = 0;
  std::vector<uint8_t> data;

  void Serialize(Serializer& ser, uint32_t ver) { ser | process_koid | address | data; }
};

struct WriteMemoryReply {
  debug::Status status;

  void Serialize(Serializer& ser, uint32_t ver) { ser | status; }
};

struct LoadInfoHandleTableRequest {
  uint64_t process_koid = 0;

  void Serialize(Serializer& ser, uint32_t ver) { ser | process_koid; }
};
struct LoadInfoHandleTableReply {
  debug::Status status;
  std::vector<InfoHandle> handles;

  void Serialize(Serializer& ser, uint32_t ver) { ser | status | handles; }
};

struct UpdateGlobalSettingsRequest {
  // Updates how the default strategy for handling a particular exception type.
  struct UpdateExceptionStrategy {
    ExceptionType type = ExceptionType::kNone;
    ExceptionStrategy value = ExceptionStrategy::kNone;

    void Serialize(Serializer& ser, uint32_t ver) { ser | type | value; }
  };

  std::vector<UpdateExceptionStrategy> exception_strategies;

  void Serialize(Serializer& ser, uint32_t ver) { ser | exception_strategies; }
};

struct UpdateGlobalSettingsReply {
  debug::Status status;

  void Serialize(Serializer& ser, uint32_t ver) { ser | status; }
};

struct SaveMinidumpRequest {
  uint64_t process_koid = 0;

  void Serialize(Serializer& ser, uint32_t ver) { ser | process_koid; }
};

struct SaveMinidumpReply {
  debug::Status status;
  std::vector<uint8_t> core_data;

  void Serialize(Serializer& ser, uint32_t ver) { ser | status | core_data; }
};

// ReadRegisters ---------------------------------------------------------------

struct ReadRegistersRequest {
  ProcessThreadId id;

  // What categories do we want to receive data from.
  std::vector<debug::RegisterCategory> categories;

  void Serialize(Serializer& ser, uint32_t ver) { ser | id | categories; }
};

struct ReadRegistersReply {
  std::vector<debug::RegisterValue> registers;

  void Serialize(Serializer& ser, uint32_t ver) { ser | registers; }
};

// WriteRegisters --------------------------------------------------------------

struct WriteRegistersRequest {
  ProcessThreadId id;
  std::vector<debug::RegisterValue> registers;

  void Serialize(Serializer& ser, uint32_t ver) { ser | id | registers; }
};

struct WriteRegistersReply {
  debug::Status status;

  // The latest registers from all affected categories after the write.
  //
  // This allows clients to validate that the change actually took effect. All known registers
  // from all categories changed by the write request will be sent.
  std::vector<debug::RegisterValue> registers;

  void Serialize(Serializer& ser, uint32_t ver) { ser | status | registers; }
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

  void Serialize(Serializer& ser, uint32_t ver) {
    ser | timestamp | type | koid | name | component;
  }
};

// Data for process destroyed messages (process created messages are in
// response to launch commands so is just the reply to that message).
struct NotifyProcessExiting {
  uint64_t timestamp = kTimestampDefault;
  uint64_t process_koid = 0;
  int64_t return_code = 0;

  void Serialize(Serializer& ser, uint32_t ver) { ser | timestamp | process_koid | return_code; }
};

// Data for thread created.
struct NotifyThreadStarting {
  uint64_t timestamp = kTimestampDefault;
  ThreadRecord record;

  void Serialize(Serializer& ser, uint32_t ver) { ser | timestamp | record; }
};

// Data for thread destroyed.
struct NotifyThreadExiting {
  uint64_t timestamp = kTimestampDefault;
  ThreadRecord record;

  void Serialize(Serializer& ser, uint32_t ver) { ser | timestamp | record; }
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

  void Serialize(Serializer& ser, uint32_t ver) {
    ser | timestamp | thread | type | exception | hit_breakpoints | other_affected_threads |
        memory_blocks;
  }
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

  void Serialize(Serializer& ser, uint32_t ver) {
    ser | timestamp | process_koid | modules | stopped_threads;
  }
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

  void Serialize(Serializer& ser, uint32_t ver) {
    ser | timestamp | process_koid | type | data | more_data_available;
  }
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

    void Serialize(Serializer& ser, uint32_t ver) { ser | file | function | line; }
  };

  Severity severity = Severity::kInfo;
  Location location;
  std::string log;

  void Serialize(Serializer& ser, uint32_t ver) { ser | timestamp | severity | location | log; }
};

// Notify that a component has started.
// Only components of interest, i.e., those matching at least one of the filters, will be notified.
struct NotifyComponentStarting {
  uint64_t timestamp = kTimestampDefault;

  ComponentInfo component;

  void Serialize(Serializer& ser, uint32_t ver) { ser | timestamp | component; }
};

// Notify that a component has exited.
// Only components of interest, i.e., those matching at least one of the filters, will be notified.
struct NotifyComponentExiting {
  static constexpr uint32_t kSupportedSinceVersion = 52;

  uint64_t timestamp = kTimestampDefault;

  ComponentInfo component;

  void Serialize(Serializer& ser, uint32_t ver) { ser | timestamp | component; }
};

#pragma pack(pop)

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_IPC_PROTOCOL_H_
