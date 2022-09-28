// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_IPC_RECORDS_H_
#define SRC_DEVELOPER_DEBUG_IPC_RECORDS_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "src/developer/debug/ipc/automation_instruction.h"
#include "src/developer/debug/shared/address_range.h"
#include "src/developer/debug/shared/register_id.h"
#include "src/developer/debug/shared/register_value.h"
#include "src/developer/debug/shared/serialization.h"

namespace debug_ipc {

#pragma pack(push, 8)

enum class ExceptionType : uint32_t {
  // No current exception, used as placeholder or to indicate not set.
  kNone = 0,

  // Zircon defines this as a sort of catch-all exception.
  kGeneral,

  // The usual band of execution traps.
  kPageFault,
  kUndefinedInstruction,
  kUnalignedAccess,

  // Indicates the process was killed due to misusing a syscall, e.g. passing a bad handle.
  kPolicyError,

  // Synthetic exeptions used by zircon to communicated with the debugger. The debug agent generally
  // shouldn't pass these on, but we should recognize them at least.
  kThreadStarting,
  kThreadExiting,
  kProcessStarting,

  // Hardware breakpoints are issues by the CPU via debug registers.
  kHardwareBreakpoint,

  // HW exceptions triggered on memory read/write.
  kWatchpoint,

  // Single-step completion issued by the CPU.
  kSingleStep,

  // Software breakpoint. This will be issued when a breakpoint is hit and when the debugged program
  // manually issues a breakpoint instruction.
  kSoftwareBreakpoint,

  // Indicates this exception is not a real CPU exception but was generated internally for the
  // purposes of sending a stop notification. The frontend uses this value when the thread didn't
  // actually do anything, but the should be updated as if it hit an exception.
  kSynthetic,

  // For exception codes the debugger doesn't recognize.
  kUnknown,

  kLast  // Not an actual exception type, for range checking.
};
const char* ExceptionTypeToString(ExceptionType);
bool IsDebug(ExceptionType);

// Exception handling strategy.
enum class ExceptionStrategy : uint32_t {
  // No current exception, used as placeholder or to indicate not set.
  kNone = 0,

  // Indicates that the debugger only gets the first chance to handle the
  // exception, before the thread and process-level handlers.
  kFirstChance,

  // Indicates that the debugger also gets a second first chance to handle
  //  the exception after process-level handler.
  kSecondChance,

  kLast,  // Not an actual exception strategy, for range checking.
};

const char* ExceptionStrategyToString(ExceptionStrategy);

std::optional<ExceptionStrategy> ToExceptionStrategy(uint32_t raw_value);

std::optional<uint32_t> ToRawValue(ExceptionStrategy strategy);

// A process+thread koid pair for referring to a thread. While a thread koid is globally unique and
// doesn't technically need a process koid to scope it, most code deals with a process/thread
// hierarchy so maintaining both is more convenient.
struct ProcessThreadId {
  uint64_t process = 0;
  uint64_t thread = 0;

  bool operator==(const ProcessThreadId& other) const {
    return process == other.process && thread == other.thread;
  }
  bool operator!=(const ProcessThreadId& other) const { return !operator==(other); }

  // For ordered containers.
  bool operator<(const ProcessThreadId& other) const {
    return std::tie(process, thread) < std::tie(other.process, other.thread);
  }

  void Serialize(Serializer& ser, uint32_t ver) { ser | process | thread; }
};

struct ExceptionRecord {
  ExceptionRecord() { memset(&arch, 0, sizeof(Arch)); }

  // Race conditions or other errors can conspire to mean the exception records are not valid. In
  // order to differentiate this case from "0" addresses, this flag indicates validity of the "arch"
  // union.
  bool valid = false;

  union Arch {
    // Exception record for x64.
    struct {
      uint64_t vector;
      uint64_t err_code;
      uint64_t cr2;
    } x64;

    // Exception record for ARM64.
    struct {
      uint32_t esr;
      uint64_t far;
    } arm64;
  } arch;

  ExceptionStrategy strategy = ExceptionStrategy::kNone;

  void Serialize(Serializer& ser, uint32_t ver) {
    ser | valid;
    ser.SerializeBytes(&arch, sizeof(Arch));
    ser | strategy;
  }
};

struct ComponentInfo {
  std::string moniker;
  std::string url;

  void Serialize(Serializer& ser, uint32_t ver) { ser | moniker | url; }
};

// Note: see "ps" source:
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/sys/bin/psutils/ps.c
struct ProcessTreeRecord {
  enum class Type : uint32_t { kJob, kProcess };

  Type type = Type::kJob;
  uint64_t koid = 0;
  std::string name;

  // The following fields are only valid on kJob and will be skipped if type is kProcess.

  // The component information if the current job is the root job of an ELF component.
  std::optional<ComponentInfo> component;

  std::vector<ProcessTreeRecord> children;

  void Serialize(Serializer& ser, uint32_t ver) {
    ser | type | koid | name;
    if (type == Type::kJob) {
      ser | component | children;
    }
  }
};

struct StackFrame {
  StackFrame() = default;
  StackFrame(uint64_t ip, uint64_t sp, uint64_t cfa = 0, std::vector<debug::RegisterValue> r = {})
      : ip(ip), sp(sp), cfa(cfa), regs(std::move(r)) {}

  // Comparisons (primarily for tests).
  bool operator==(const StackFrame& other) const {
    return ip == other.ip && sp == other.sp && cfa == other.cfa && regs == other.regs;
  }
  bool operator!=(const StackFrame& other) const { return !operator==(other); }

  // Instruction pointer.
  uint64_t ip = 0;

  // Stack pointer.
  uint64_t sp = 0;

  // Canonical frame address. This is the stack pointer of the previous
  // frame at the time of the call. 0 if unknown.
  uint64_t cfa = 0;

  // Known general registers for this stack frame. See IsGeneralRegister() for
  // which registers are counted as "general".
  //
  // Every frame should contain the register for the IP and SP for the current
  // architecture (duplicating the above two fields).
  std::vector<debug::RegisterValue> regs;

  void Serialize(Serializer& ser, uint32_t ver) { ser | ip | sp | cfa | regs; }
};

struct ThreadRecord {
  enum class State : uint32_t {
    kNew = 0,  // The thread is newly created and running.
    kRunning,
    kSuspended,
    kBlocked,
    kDying,
    kDead,
    kCoreDump,

    kLast  // Not an actual thread state, for range checking.
  };
  static const char* StateToString(State);

  enum class BlockedReason : uint32_t {
    kNotBlocked = 0,  // Used when State isn't kBlocked.

    kException,
    kSleeping,
    kFutex,
    kPort,
    kChannel,
    kWaitOne,
    kWaitMany,
    kInterrupt,
    kPager,

    kLast  // Not an actual blocked reason, for range checking.
  };
  static const char* BlockedReasonToString(BlockedReason);

  // Indicates how much of the stack was attempted to be retrieved in this
  // call. This doesn't indicate how many stack frames were actually retrieved.
  // For example, there could be no stack frames because they weren't
  // requested, or there could be no stack frames due to an error.
  enum class StackAmount : uint32_t {
    // A backtrace was not attempted. This will always be the case if the
    // thread is neither suspended nor blocked in an exception.
    kNone = 0,

    // The frames vector contains a minimal stack only (if available) which
    // is defined as the top two frames. This is used when the stack frames
    // have not been specifically requested since retrieving the full stack
    // can be slow. The frames can still be less than 2 if there was an error
    // or if there is only one stack frame.
    kMinimal,

    // The frames are the full stack trace (up to some maximum).
    kFull,

    kLast  // Not an actual state, for range checking.
  };

  ProcessThreadId id;
  std::string name;
  State state = State::kNew;
  // Only valid when state is kBlocked.
  BlockedReason blocked_reason = BlockedReason::kNotBlocked;
  StackAmount stack_amount = StackAmount::kNone;

  // The frames of the top of the stack when the thread is in suspended or blocked in an exception
  // (if possible). See stack_amnount for how to interpret this.
  //
  // This could still be empty in the "kMinimal" or "kFull" cases if retrieval failed, which can
  // happen in some valid race conditions if the thread was killed out from under the debug agent.
  std::vector<StackFrame> frames;

  void Serialize(Serializer& ser, uint32_t ver) {
    ser | id | name | state | blocked_reason | stack_amount | frames;
  }
};

struct ProcessRecord {
  uint64_t process_koid = 0;
  std::string process_name;

  // The component information if the process is running in a component. Not hooked up yet.
  std::optional<ComponentInfo> component;

  std::vector<ThreadRecord> threads;

  void Serialize(Serializer& ser, uint32_t ver) {
    ser | process_koid | process_name | component | threads;
  }
};

struct MemoryBlock {
  // Begin address of this memory.
  uint64_t address = 0;

  // When true, indicates this is valid memory, with the data containing the
  // memory. False means that this range is not mapped in the process and the
  // data will be empty.
  bool valid = false;

  // Length of this range. When valid == true, this will be the same as
  // data.size(). When valid == false, this will be whatever the length of
  // the invalid region is, and data will be empty.
  uint32_t size = 0;

  // The actual memory. Filled in only if valid == true.
  std::vector<uint8_t> data;

  void Serialize(Serializer& ser, uint32_t ver) { ser | address | valid | size | data; }
};

struct ProcessBreakpointSettings {
  // The process is required to be nonzero. A zero thread ID indicates this is a process-wide
  // breakpoint. Otherwise, this is the thread to break.
  ProcessThreadId id;

  // Address to break at.
  uint64_t address = 0;

  // Range is used for watchpoints.
  debug::AddressRange address_range;

  void Serialize(Serializer& ser, uint32_t ver) { ser | id | address | address_range; }
};

// What threads to stop when the breakpoint is hit. These are ordered such that the integer values
// increase for larger scopes.
enum class Stop : uint32_t {
  kNone = 0,  // Don't stop anything but accumulate hit counts.
  kThread,    // Stop only the thread that hit the breakpoint.
  kProcess,   // Stop all threads of the process that hit the breakpoint.
  kAll        // Stop all threads of all processes attached to the debugger.
};

// NOTE: read-only could be added in the future as arm64 supports them. They're not added today as
//       x64 does not support them and presenting a common platform is cleaner for now.
enum class BreakpointType : uint32_t {
  kSoftware = 0,  // Software code execution.
  kHardware,      // Hardware code execution.
  kReadWrite,     // Hardware read/write.
  kWrite,         // Hardware write.

  kLast,  // Not a real type, end marker.
};
const char* BreakpointTypeToString(BreakpointType);

// Read, ReadWrite and Write are considered watchpoint types.
bool IsWatchpointType(BreakpointType);

constexpr uint32_t kDebugAgentInternalBreakpointId = static_cast<uint32_t>(-1);

struct BreakpointSettings {
  // The ID if this breakpoint. This is assigned by the client. This is different than the ID in
  // the console frontend which can be across mutliple processes or may match several addresses in
  // a single process.
  //
  // The ID kDebugAgentInternalBreakpointId is reserved for internal use by the backend.
  uint32_t id = 0;

  BreakpointType type = BreakpointType::kSoftware;

  // Name used to recognize a breakpoint. Useful for debugging purposes. Optional.
  std::string name;

  // When set, the breakpoint will automatically be removed as soon as it is
  // hit.
  bool one_shot = false;

  // What should stop when the breakpoint is hit.
  Stop stop = Stop::kAll;

  // Processes to which this breakpoint applies.
  //
  // If any process specifies a nonzero thread_koid, it must be the only process (a breakpoint can
  // apply either to all threads in a set of processes, or exactly one thread globally).
  std::vector<ProcessBreakpointSettings> locations;

  // Handles the automatic collection of memory if it's requested.
  bool has_automation = false;

  std::vector<debug_ipc::AutomationInstruction> instructions;

  void Serialize(Serializer& ser, uint32_t ver) {
    ser | id | type | name | one_shot | stop | locations | has_automation | instructions;
  }
};

struct BreakpointStats {
  uint32_t id = 0;
  uint32_t hit_count = 0;

  // On a "breakpoint hit" message from the debug agent, if this flag is set,
  // the agent has deleted the breakpoint because it was a one-shot breakpoint.
  // Whenever a client gets a breakpoint hit with this flag set, it should
  // clear the local state associated with the breakpoint.
  bool should_delete = false;

  void Serialize(Serializer& ser, uint32_t ver) { ser | id | hit_count | should_delete; }
};

// Information on one loaded module.
struct Module {
  std::string name;            // The main executable binary will normally have an empty name.
  uint64_t base = 0;           // Load address of this file.
  uint64_t debug_address = 0;  // Link map address for this module.
  std::string build_id;

  void Serialize(Serializer& ser, uint32_t ver) { ser | name | base | debug_address | build_id; }
};

struct AddressRegion {
  std::string name;
  uint64_t base = 0;
  uint64_t size = 0;
  uint64_t depth = 0;
  uint32_t mmu_flags = 0;
  uint64_t vmo_koid = 0;
  uint64_t vmo_offset = 0;
  uint64_t committed_pages = 0;

  void Serialize(Serializer& ser, uint32_t ver) {
    ser | name | base | size | depth | mmu_flags | vmo_koid | vmo_offset | committed_pages;
  }
};

// LoadInfoHandleTable -----------------------------------------------------------------------------

// VMO-specific handle information from zx_info_vmo that's not in the InfoHandle structure.
struct InfoHandleVmo {
  char name[32];  // Needs to be POD for use in union below, and 32 is the max from the kernel.
  uint64_t size_bytes;
  uint64_t parent_koid;
  uint64_t num_children;
  uint64_t num_mappings;
  uint64_t share_count;
  uint32_t flags;
  uint64_t committed_bytes;
  uint32_t cache_policy;
  uint64_t metadata_bytes;
  uint64_t committed_change_events;
};

// This structure is assumed to be entirely POD.
struct InfoHandle {
  // Provide 0-init that covers the union.
  InfoHandle() { memset(this, 0, sizeof(InfoHandle)); }

  // Standard information from zx_info_handle_extended.
  //
  // There is a special case for a VMO. It is possible to have a VMO mapped without a handle to it.
  // These will appear here but the handle_value will be 0.
  uint32_t type;
  uint32_t handle_value;
  uint32_t rights;
  uint32_t reserved;
  uint64_t koid;
  uint64_t related_koid;
  uint64_t peer_owner_koid;

  // Type-specific handle information.
  union {
    InfoHandleVmo vmo;  // Valid when type == ZX_OBJ_TYPE_VMO.
    // Other types go here.
  } ext;

  void Serialize(Serializer& ser, uint32_t ver) { ser.SerializeBytes(this, sizeof(*this)); }
};

// Filters -----------------------------------------------------------------------------------------
struct Filter {
  enum class Type : uint32_t {
    kUnset,
    kProcessNameSubstr,
    kProcessName,
    kComponentName,
    kComponentUrl,
    kComponentMoniker,

    kLast,
  } type = Type::kUnset;
  static const char* TypeToString(Type);

  std::string pattern;
  uint64_t job_koid = 0;  // must be 0 when type is kComponent*.

  void Serialize(Serializer& ser, uint32_t ver) { ser | type | pattern | job_koid; }
};

#pragma pack(pop)

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_IPC_RECORDS_H_
