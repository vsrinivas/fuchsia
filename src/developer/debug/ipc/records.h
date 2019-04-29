// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_DEBUG_IPC_RECORDS_H_
#define GARNET_LIB_DEBUG_IPC_RECORDS_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "src/developer/debug/ipc/register_desc.h"

namespace debug_ipc {

#pragma pack(push, 8)

// Note: see "ps" source:
// https://fuchsia.googlesource.com/fuchsia/+/master/zircon/system/uapp/psutils/ps.c
struct ProcessTreeRecord {
  enum class Type : uint32_t { kJob, kProcess };

  Type type = Type::kJob;
  uint64_t koid = 0;
  std::string name;

  std::vector<ProcessTreeRecord> children;
};

struct StackFrame {
  StackFrame() = default;
  StackFrame(uint64_t ip, uint64_t bp, uint64_t sp) : ip(ip), bp(bp), sp(sp) {}

  // Instruction pointer.
  uint64_t ip = 0;

  // Frame base pointer. This may be invalid if the code was compiled without
  // frame pointers.
  uint64_t bp = 0;

  // Stack pointer.
  uint64_t sp = 0;
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

  // The frames of the top of the stack when the thread is in suspended or
  // blocked in an exception. See stack_amnount for how to interpret this.
  // Note that this could still be empty in the "kMinimal" or "kFull" cases
  // if retrieval failed.
  std::vector<StackFrame> frames;
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

  // Zero indicates this is a process-wide breakpoint. Otherwise, this
  // indicates the thread to break.
  uint64_t thread_koid = 0;

  // Address to break at.
  uint64_t address = 0;
};

// What threads to stop when the breakpoint is hit.
enum class Stop : uint32_t {
  kAll,      // Stop all threads of all processes attached to the debugger.
  kProcess,  // Stop all threads of the process that hit the breakpoint.
  kThread,   // Stop only the thread that hit the breakpoint.
  kNone      // Don't stop anything but accumulate hit counts.
};

enum class BreakpointType : uint32_t {
  kSoftware,
  kHardware,
  kWatchpoint,
  kLast,
};
const char* BreakpointTypeToString(BreakpointType);

struct BreakpointSettings {
  // The ID if this breakpoint. This is assigned by the client. This is
  // different than the ID in the console frontend which can be across mutliple
  // processes or may match several addresses in a single process.
  uint32_t id = 0;

  // When set, the breakpoint will automatically be removed as soon as it is
  // hit.
  bool one_shot = false;

  // What should stop when the breakpoint is hit.
  Stop stop = Stop::kAll;

  // Processes to which this breakpoint applies.
  //
  // If any process specifies a nonzero thread_koid, it must be the only
  // process (a breakpoint can apply either to all threads in a set of
  // processes, or exactly one thread globally).
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

// Watchpoints -----------------------------------------------------------------

struct AddressRange {
  uint64_t begin = 0;
  uint64_t end = 0;  // Non-inclusive.
};

struct ProcessWatchpointSetings {
  // Must be non-zero.
  uint64_t process_koid = 0;

  // Zero indicates this is a process-wide breakpoint. Otherwise, this
  // indicates the thread to break.
  uint64_t thread_koid = 0;

  AddressRange range = {};
};

// TODO(donosoc): This settings is not exhaustive, but it's enough to get the
//                plumbing (get it?) going.
struct WatchpointSettings {
  // Assigned by the client. Analogous to BreakpointSettings::breakpoint_id.
  uint32_t id = 0;

  // When set, the watchpoint will automatically be removed as soon as it is
  // hit.
  bool one_shot = false;

  // What should stop when the watchpoint is hit.
  Stop stop = Stop::kAll;

  std::vector<ProcessWatchpointSetings> locations;
};

// Information on one loaded module.
struct Module {
  std::string name;
  uint64_t base = 0;  // Load address of this file.
  std::string build_id;
};

struct AddressRegion {
  std::string name;
  uint64_t base;
  uint64_t size;
  uint64_t depth;
};

// ReadRegisters ---------------------------------------------------------------

// Value representing a particular register.
struct Register {
  RegisterID id = RegisterID::kUnknown;

  // This data is stored in the architecture native's endianness
  // (eg. the result of running memcpy over the data).
  std::vector<uint8_t> data;
};

// Division of RegisterSections, according to their usage.
struct RegisterCategory {
  // Categories will always be sorted from lower to upper
  enum class Type : uint32_t {
    kGeneral,
    kFP,
    kVector,
    kDebug,

    kNone,
  };
  static const char* TypeToString(Type);
  static Type RegisterIDToCategory(RegisterID);

  Type type = Type::kNone;
  std::vector<Register> registers;
};

struct ConfigAction {
  enum class Type : uint32_t {
    // Quit whenever the connection shutdowns.
    kQuitOnExit,    // Values are "false" | "true"

    kLast,  // Not valid.
  };
  static const char* TypeToString(Type);

  Type type = Type::kLast;

  // Each action uses a different set of values.
  std::string value;
};

#pragma pack(pop)

}  // namespace debug_ipc

#endif  // GARNET_LIB_DEBUG_IPC_RECORDS_H_
