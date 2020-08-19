// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_IPC_RECORDS_H_
#define SRC_DEVELOPER_DEBUG_IPC_RECORDS_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "src/developer/debug/ipc/register_desc.h"
#include "src/developer/debug/shared/address_range.h"

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
};

// Note: see "ps" source:
// https://fuchsia.googlesource.com/fuchsia/+/master/src/sys/bin/psutils/ps.c
struct ProcessTreeRecord {
  enum class Type : uint32_t { kJob, kProcess };

  Type type = Type::kJob;
  uint64_t koid = 0;
  std::string name;

  std::vector<ProcessTreeRecord> children;
};

// Value representing a particular register.
struct Register {
  Register() = default;
  Register(RegisterID rid, std::vector<uint8_t> d) : id(rid), data(std::move(d)) {}

  // Constructs from a size and a pointed-to data buffer in machine-endianness.
  Register(RegisterID rid, size_t byte_size, const void* bytes) : id(rid) {
    data.resize(byte_size);
    memcpy(&data[0], bytes, byte_size);
  }

  // Constructs a sized value for the current platform.
  Register(RegisterID rid, uint64_t val) : id(rid) {
    data.resize(sizeof(val));
    memcpy(&data[0], &val, sizeof(val));
  }
  Register(RegisterID rid, uint32_t val) : id(rid) {
    data.resize(sizeof(val));
    memcpy(&data[0], &val, sizeof(val));
  }
  Register(RegisterID rid, uint16_t val) : id(rid) {
    data.resize(sizeof(val));
    memcpy(&data[0], &val, sizeof(val));
  }
  Register(RegisterID rid, uint8_t val) : id(rid) {
    data.resize(sizeof(val));
    memcpy(&data[0], &val, sizeof(val));
  }

  // Retrieves the low up-to-128 bits of the register value as a number.
  __int128 GetValue() const;

  // Comparisons (primarily for tests).
  bool operator==(const Register& other) const { return id == other.id && data == other.data; }
  bool operator!=(const Register& other) const { return !operator==(other); }

  RegisterID id = RegisterID::kUnknown;

  // This data is stored in the architecture's native endianness
  // (eg. the result of running memcpy over the data).
  std::vector<uint8_t> data;
};

struct StackFrame {
  StackFrame() = default;
  StackFrame(uint64_t ip, uint64_t sp, uint64_t cfa = 0, std::vector<Register> r = {})
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
  std::vector<Register> regs;
};

struct ThreadRecord {
  enum class State : uint32_t {
    kNew = 0,
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

  uint64_t process_koid = 0;
  uint64_t thread_koid = 0;
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
};

struct ProcessRecord {
  uint64_t process_koid = 0;
  std::string process_name;

  std::vector<ThreadRecord> threads;
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
};

struct ProcessBreakpointSettings {
  // Required to be nonzero.
  uint64_t process_koid = 0;

  // Zero indicates this is a process-wide breakpoint. Otherwise, this is the thread to break.
  uint64_t thread_koid = 0;

  // Address to break at.
  uint64_t address = 0;

  // Range is used for watchpoints.
  AddressRange address_range;
};

// What threads to stop when the breakpoint is hit.
enum class Stop : uint32_t {
  kAll,      // Stop all threads of all processes attached to the debugger.
  kProcess,  // Stop all threads of the process that hit the breakpoint.
  kThread,   // Stop only the thread that hit the breakpoint.
  kNone      // Don't stop anything but accumulate hit counts.
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
};

struct BreakpointStats {
  uint32_t id = 0;
  uint32_t hit_count = 0;

  // On a "breakpoint hit" message from the debug agent, if this flag is set,
  // the agent has deleted the breakpoint because it was a one-shot breakpoint.
  // Whenever a client gets a breakpoint hit with this flag set, it should
  // clear the local state associated with the breakpoint.
  bool should_delete = false;
};

// Information on one loaded module.
struct Module {
  std::string name;
  uint64_t base = 0;           // Load address of this file.
  uint64_t debug_address = 0;  // Link map address for this module.
  std::string build_id;
};

struct AddressRegion {
  std::string name;
  uint64_t base;
  uint64_t size;
  uint64_t depth;
};

// ReadRegisters ---------------------------------------------------------------

struct ConfigAction {
  enum class Type : uint32_t {
    // Quit whenever the connection shutdowns.
    kQuitOnExit,  // Values are "false" | "true"

    kLast,  // Not valid.
  };
  static const char* TypeToString(Type);

  Type type = Type::kLast;

  // Each action uses a different set of values.
  std::string value;
};

// LoadInfoHandleTable -----------------------------------------------------------------------------

struct InfoHandleExtended {
  uint32_t type = 0;
  uint32_t handle_value = 0;
  uint32_t rights = 0;
  uint32_t reserved = 0;
  uint64_t koid = 0;
  uint64_t related_koid = 0;
  uint64_t peer_owner_koid = 0;
};

#pragma pack(pop)

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_IPC_RECORDS_H_
