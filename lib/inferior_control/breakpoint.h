// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "lib/fxl/macros.h"

namespace inferior_control {

class Process;
class Thread;
class ProcessBreakpointSet;
class ThreadBreakpointSet;

// Represents a breakpoint.
class Breakpoint {
 public:
  Breakpoint(uintptr_t address, size_t kind);
  virtual ~Breakpoint() = default;

  // Inserts the breakpoint at the memory address it was initialized with.
  // Returns true on success. Returns false on failure, e.g. if the breakpoint
  // was already inserted or there was an error while inserting it.
  virtual bool Insert() = 0;

  // Removes the breakpoint. Returns true on success, false on failure.
  virtual bool Remove() = 0;

  // Returns true if Insert() has been called successfully on this breakpoint.
  virtual bool IsInserted() const = 0;

  uintptr_t address() const { return address_; }
  size_t kind() const { return kind_; }

 private:
  Breakpoint() = default;

  uintptr_t address_;
  size_t kind_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Breakpoint);
};

class ProcessBreakpoint : public Breakpoint {
 protected:
  ProcessBreakpoint(uintptr_t address, size_t kind,
                    ProcessBreakpointSet* owner);
  ProcessBreakpointSet* owner() const { return owner_; }

 private:
  ProcessBreakpoint() = default;

  ProcessBreakpointSet* owner_;  // weak

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessBreakpoint);
};

// Represents a software breakpoint.
class SoftwareBreakpoint final : public ProcessBreakpoint {
 public:
  SoftwareBreakpoint(uintptr_t address, size_t kind,
                     ProcessBreakpointSet* owner);
  ~SoftwareBreakpoint() override;

  // Breakpoint overrides
  bool Insert() override;
  bool Remove() override;
  bool IsInserted() const override;

 private:
  SoftwareBreakpoint() = default;

  // Contains the bytes of the original instructions that were overriden while
  // inserting this breakpoint. We keep a copy of these here to restore the
  // original bytes while removing this breakpoint.
  std::vector<uint8_t> original_bytes_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SoftwareBreakpoint);
};

// Represents a collection of breakpoints managed by a process and defines
// operations for adding and removing them.
class ProcessBreakpointSet final {
 public:
  explicit ProcessBreakpointSet(Process* process);
  ~ProcessBreakpointSet() = default;

  // Returns a pointer to the process that this object belongs to.
  Process* process() const { return process_; }

  // Inserts a software breakpoint at the specified memory address with the
  // given kind. |kind| is an architecture dependent parameter that specifies
  // how many bytes the software breakpoint spans. Returns true on success or
  // false on failure.
  bool InsertSoftwareBreakpoint(uintptr_t address, size_t kind);

  // Removes the software breakpoint that was previously inserted at the given
  // address. Returns false if there is an error of a breakpoint was not
  // previously inserted at the given address. Returns true on success.
  bool RemoveSoftwareBreakpoint(uintptr_t address);

 private:
  Process* process_;  // weak

  // All currently inserted breakpoints.
  std::unordered_map<uintptr_t, std::unique_ptr<Breakpoint>> breakpoints_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessBreakpointSet);
};

class ThreadBreakpoint : public Breakpoint {
 protected:
  ThreadBreakpoint(uintptr_t address, size_t kind, ThreadBreakpointSet* owner);
  ThreadBreakpointSet* owner() const { return owner_; }

 private:
  ThreadBreakpoint() = default;

  ThreadBreakpointSet* owner_;  // weak

  FXL_DISALLOW_COPY_AND_ASSIGN(ThreadBreakpoint);
};

// Represents a single-step breakpoint.
// This is for h/w based single-stepping only.
class SingleStepBreakpoint final : public ThreadBreakpoint {
 public:
  SingleStepBreakpoint(uintptr_t address, ThreadBreakpointSet* owner);
  ~SingleStepBreakpoint() override;

  // Breakpoint overrides
  bool Insert() override;
  bool Remove() override;
  bool IsInserted() const override;

 private:
  SingleStepBreakpoint() = default;

  bool inserted_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(SingleStepBreakpoint);
};

// Represents a collection of breakpoints managed by a thread and defines
// operations for adding and removing them.
class ThreadBreakpointSet final {
 public:
  explicit ThreadBreakpointSet(Thread* thread);
  ~ThreadBreakpointSet() = default;

  // Returns a pointer to the thread that this object belongs to.
  Thread* thread() const { return thread_; }

  // Inserts a single-step breakpoint at the specified memory address with the
  // given kind. |address| is recorded as the current pc value, at the moment
  // for bookkeeping purposes.
  // Returns true on success or false on failure.
  bool InsertSingleStepBreakpoint(uintptr_t address);

  // Removes the single-step breakpoint that was previously inserted.
  // Returns true on success or false on failure.
  bool RemoveSingleStepBreakpoint();

  // Returns true if a single-step breakpoint is inserted.
  bool SingleStepBreakpointInserted();

 private:
  Thread* thread_;  // weak

  // All currently inserted breakpoints.
  std::unordered_map<uintptr_t, std::unique_ptr<ThreadBreakpoint>> breakpoints_;

  // There can be only one singlestep breakpoint.
  std::unique_ptr<ThreadBreakpoint> single_step_breakpoint_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ThreadBreakpointSet);
};

}  // namespace inferior_control
