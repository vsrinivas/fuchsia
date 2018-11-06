// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_DEBUG_IPC_RECORDS_H_
#define GARNET_LIB_DEBUG_IPC_RECORDS_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "garnet/lib/debug_ipc/register_desc.h"

namespace debug_ipc {

#pragma pack(push, 8)

// Note: see "ps" source:
// https://fuchsia.googlesource.com/zircon/+/master/system/uapp/psutils/ps.c
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

  uint64_t koid = 0;
  std::string name;
  State state = State::kNew;

  // The frames of the top of the stack when the thread is suspended. This will
  // contain the top 2 frames (the current one and the caller) if they are
  // available. It will always contain at least one frame when suspended which
  // contains the current IP.
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
};

struct BreakpointSettings {
  // The ID if this breakpoint. This is assigned by the client. This is
  // different than the ID in the console frontend which can be across mutliple
  // processes or may match several addresses in a single process.
  uint32_t breakpoint_id = 0;

  // When set, the breakpoint will automatically be removed as soon as it is
  // hit.
  bool one_shot = false;

  // What should stop when the breakpoint is hit.
  Stop stop = Stop::kAll;

  // What kind of breakpoint this is.
  BreakpointType type = BreakpointType::kSoftware;

  // Processes to which this breakpoint applies.
  //
  // If any process specifies a nonzero thread_koid, it must be the only
  // process (a breakpoint can apply either to all threads in a set of
  // processes, or exactly one thread globally).
  std::vector<ProcessBreakpointSettings> locations;
};

struct BreakpointStats {
  uint32_t breakpoint_id = 0;
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
  uint64_t base = 0;  // Load address of this file.
  std::string build_id;
};

struct AddressRegion {
  std::string name;
  uint64_t base;
  uint64_t size;
  uint64_t depth;
};

// Registers -------------------------------------------------------------------

// Value representing a particular register.
struct Register {
  RegisterID id;
  // This data is stored in the architecture native's endianness
  // (eg. the result of running memcpy over the data).
  std::vector<uint8_t> data;
};

// Division of RegisterSections, according to their usage.
struct RegisterCategory {
  // Categories will always be sorted from lower to upper
  enum class Type : uint32_t {
    kGeneral,
    kFloatingPoint,
    kVector,
    kDebug,

    kNone,
  };
  Type type = Type::kNone;
  std::vector<Register> registers;
};

#pragma pack(pop)

}  // namespace debug_ipc

#endif  // GARNET_LIB_DEBUG_IPC_RECORDS_H_
