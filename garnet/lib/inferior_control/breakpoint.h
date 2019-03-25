// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <unordered_map>
#include <vector>

#include <lib/fxl/macros.h>

namespace inferior_control {

class Process;
class Thread;
class ProcessBreakpointSet;
class ThreadBreakpointSet;

// Represents a breakpoint.
class Breakpoint {
 public:
  Breakpoint(zx_vaddr_t address, size_t size);
  virtual ~Breakpoint() = default;

  // Inserts the breakpoint at the memory address it was initialized with.
  // Returns true on success. Returns false on failure, e.g. if the breakpoint
  // was already inserted or there was an error while inserting it.
  virtual bool Insert() = 0;

  // Removes the breakpoint. Returns true on success, false on failure.
  virtual bool Remove() = 0;

  // Returns true if Insert() has been called successfully on this breakpoint.
  virtual bool IsInserted() const = 0;

  zx_vaddr_t address() const { return address_; }
  size_t size() const { return size_; }

 private:
  Breakpoint() = default;

  zx_vaddr_t address_;
  size_t size_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Breakpoint);
};

class ProcessBreakpoint : public Breakpoint {
 protected:
  ProcessBreakpoint(zx_vaddr_t address, size_t size,
                    ProcessBreakpointSet* owner);
  ProcessBreakpointSet* owner() const { return owner_; }

 private:
  ProcessBreakpointSet* owner_;  // non-owning

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessBreakpoint);
};

// Represents a software breakpoint.
class SoftwareBreakpoint final : public ProcessBreakpoint {
 public:
  // Return the size in bytes of a s/w breakpoint.
  static size_t Size();

  SoftwareBreakpoint(zx_vaddr_t address, ProcessBreakpointSet* owner);
  ~SoftwareBreakpoint() override;

  // Breakpoint overrides
  bool Insert() override;
  bool Remove() override;
  bool IsInserted() const override;

 private:
  // Contains the bytes of the original instructions that were overridden while
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

  // Inserts a software breakpoint at the specified memory address.
  // Returns true on success or false on failure.
  bool InsertSoftwareBreakpoint(zx_vaddr_t address);

  // Removes the software breakpoint that was previously inserted at the given
  // address. Returns false if there is an error of a breakpoint was not
  // previously inserted at the given address. Returns true on success.
  bool RemoveSoftwareBreakpoint(zx_vaddr_t address);

 private:
  Process* process_;  // non-owning

  // All currently inserted breakpoints.
  std::unordered_map<zx_vaddr_t, std::unique_ptr<Breakpoint>> breakpoints_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessBreakpointSet);
};

class ThreadBreakpoint : public Breakpoint {
 protected:
  ThreadBreakpoint(zx_vaddr_t address, size_t size, ThreadBreakpointSet* owner);
  ThreadBreakpointSet* owner() const { return owner_; }

 private:
  ThreadBreakpointSet* owner_;  // non-owning

  FXL_DISALLOW_COPY_AND_ASSIGN(ThreadBreakpoint);
};

// Represents a single-step breakpoint.
// This is for h/w based single-stepping only.
class SingleStepBreakpoint final : public ThreadBreakpoint {
 public:
  SingleStepBreakpoint(zx_vaddr_t address, ThreadBreakpointSet* owner);
  ~SingleStepBreakpoint() override;

  // Breakpoint overrides
  bool Insert() override;
  bool Remove() override;
  bool IsInserted() const override;

 private:
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

  // Inserts a single-step breakpoint at the specified memory address.
  // |address| is recorded as the current pc value, at the moment
  // for bookkeeping purposes.
  // Returns true on success or false on failure.
  bool InsertSingleStepBreakpoint(zx_vaddr_t address);

  // Removes the single-step breakpoint that was previously inserted.
  // Returns true on success or false on failure.
  bool RemoveSingleStepBreakpoint();

  // Returns true if a single-step breakpoint is inserted.
  bool SingleStepBreakpointInserted();

 private:
  Thread* thread_;  // non-owning

  // All currently inserted breakpoints.
  std::unordered_map<zx_vaddr_t, std::unique_ptr<ThreadBreakpoint>> breakpoints_;

  // There can be only one singlestep breakpoint.
  std::unique_ptr<ThreadBreakpoint> single_step_breakpoint_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ThreadBreakpointSet);
};

}  // namespace inferior_control
