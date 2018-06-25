// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/lib/debug_ipc/records.h"

namespace debug_ipc {

constexpr uint32_t kProtocolVersion = 2;

enum class Arch { kUnknown = 0, kX64, kArm64 };

#pragma pack(push, 8)

// A message consists of a MsgHeader followed by a serialized version of
// whatever struct is associated with that message type. Use the MessageWriter
// class to build this up, which will reserve room for the header and allows
// the structs to be appended, possibly dynamically.
struct MsgHeader {
  enum class Type : uint32_t {
    kNone = 0,
    kHello,
    kLaunch,
    kKill,
    kAttach,
    kDetach,
    kModules,
    kPause,
    kResume,
    kProcessTree,
    kThreads,
    kReadMemory,
    kRegisters,
    kAddOrChangeBreakpoint,
    kRemoveBreakpoint,
    kBacktrace,
    kAddressSpace,

    // The "notify" messages are sent unrequested from the agent to the client.
    kNotifyProcessExiting,
    kNotifyThreadStarting,
    kNotifyThreadExiting,
    kNotifyException,

    kNumMessages
  };

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

  static constexpr uint32_t kCurrentVersion = 1;

  uint64_t signature = kStreamSignature;
  uint32_t version = kCurrentVersion;
  Arch arch = Arch::kUnknown;
};

struct LaunchRequest {
  // argv[0] is the app to launch.
  std::vector<std::string> argv;
};
struct LaunchReply {
  uint32_t status = 0;  // zx_status_t value from launch, ZX_OK on success.
  uint64_t process_koid = 0;
  std::string process_name;
};

struct KillRequest {
  uint64_t process_koid = 0;
};
struct KillReply {
  uint32_t status = 0;
};

// The debug agent will follow a successful AttachReply with notifications for
// all threads currently existing in the attached process.
struct AttachRequest {
  uint64_t koid;
};
struct AttachReply {
  uint32_t status = 0;  // zx_status_t value from attaching. ZX_OK on success.
  std::string process_name;
};

struct DetachRequest {
  uint64_t process_koid = 0;
};
struct DetachReply {
  uint32_t status = 0;
};

struct PauseRequest {
  // If 0, all threads of all debugged processes will be paused.
  uint64_t process_koid = 0;

  // If 0, all threads in the given process will be paused.
  uint64_t thread_koid = 0;
};
struct PauseReply {};

struct ResumeRequest {
  enum class How : uint32_t {
    kContinue = 0,     // Continue execution without stopping.
    kStepInstruction,  // Step one machine instruction.
    kStepInRange,      // Step until control exits an address range.

    kLast  // Not a real state, used for validation.
  };

  // If 0, all threads of all debugged processes will be continued.
  uint64_t process_koid = 0;

  // If 0, all threads in the given process will be continued. Not compatible
  // with kStepInRange.
  uint64_t thread_koid = 0;

  How how = How::kContinue;

  // When how == kStepInRange, these variables define the address range to
  // step in. As long as the instruction pointer is inside
  // [range_begin, range_end), execution will continue.
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
  // A variety of race conditions could cause a breakpoint modification or
  // set to fail. For example, updating or setting a breakpoint could race
  // with the library containing that code unloading.
  //
  // The update or set will always apply the breakpoint to any contexts that
  // it can apply to (if there are multiple locations, we don't want to
  // remove them all just because one failed). Therefore, you can't
  // definitively say the breakpoint is invalid just because it has a failure
  // code here. If necessary, we can add more information in the failure.
  uint32_t status = 0;  // zx_status_t
};

struct RemoveBreakpointRequest {
  uint32_t breakpoint_id = 0;
};
struct RemoveBreakpointReply {};

struct BacktraceRequest {
  uint64_t process_koid = 0;
  uint32_t thread_koid = 0;
};
struct BacktraceReply {
  // Will be empty if the thread doesn't exist or isn't stopped.
  std::vector<StackFrame> frames;
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

// Registers -------------------------------------------------------------------

struct RegistersRequest {
  uint64_t process_koid = 0;
  uint32_t thread_koid = 0;
};
struct RegistersReply {
  std::vector<Register> registers;
};

// Notifications ---------------------------------------------------------------

// Data for process destroyed messages (process created messages are in
// response to launch commands so is just the reply to that message).
struct NotifyProcess {
  uint64_t process_koid = 0;
  int64_t return_code = 0;
};

// Data for thread created and destroyed messages.
struct NotifyThread {
  uint64_t process_koid = 0;
  ThreadRecord record;
};

// Data passed for exceptions.
struct NotifyException {
  enum class Type : uint32_t {
    kGeneral = 0,
    kHardware,
    kSoftware,

    kLast  // Not an actual exception type, for range checking.
  };

  uint64_t process_koid = 0;
  ThreadRecord thread;

  Type type = Type::kGeneral;

  // The frame of the top of the stack.
  StackFrame frame;

  // When the stop was caused by hitting a breakpoint, this vector will contain
  // the post-hit stats of every hit breakpoint (since there can be more than
  // one breakpoint at any given address).
  //
  // Be sure to check should_delete on each of these and update local state as
  // necessary.
  std::vector<BreakpointStats> hit_breakpoints;
};

#pragma pack(pop)

}  // namespace debug_ipc
