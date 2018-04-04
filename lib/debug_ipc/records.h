// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <string>
#include <vector>

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

struct ThreadRecord {
  enum class State : uint32_t {
    kNew = 0,
    kRunning,
    kSuspended,
    kBlocked,
    kDying,
    kDead,

    kLast  // Not an actual thread state, for range checking.
  };

  uint64_t koid = 0;
  std::string name;
  State state = State::kNew;
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

// What threads to stop when the breakpoint is hit.
enum class Stop : uint32_t {
  kAll,  // Stop all threads of all processes attached to the debugger.
  kProcess,  // Stop all threads of the process that hit the breakpoint.
  kThread  // Stop only the thread that hit the breakpoint.
};

struct BreakpointSettings {
  // The ID if this breakpoint. This is assigned by the client and identifies a
  // single breakpoint at a single address in a single process. This is
  // different than the ID in the console frontend which can be across mutliple
  // processes or may match several addresses in a single process.
  uint32_t breakpoint_id = 0;

  // Thread that this breakpoint applies to. If 0 it will apply to all threads
  // in the process.
  uint64_t thread_koid = 0;

  uint64_t address = 0;
  Stop stop = Stop::kAll;
};

// Information on one loaded module.
struct Module {
  std::string name;
  uint64_t base = 0;  // Load address of this file.
  // Will need more things here like build_id.
};

struct StackFrame {
  uint64_t ip = 0;  // Instruction pointer.
  uint64_t sp = 0;  // Stack pointer.
};

#pragma pack(pop)

}  // namespace debug_ipc
