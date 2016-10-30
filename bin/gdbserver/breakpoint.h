// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "lib/ftl/macros.h"

namespace debugserver {

class Process;

namespace arch {

class BreakpointSet;

// Represents a breakpoint.
class Breakpoint {
 public:
  Breakpoint(uintptr_t address, size_t kind, BreakpointSet* owner);
  virtual ~Breakpoint() = default;

  // Inserts the breakpoint at the memory address it was initialized with.
  // Returns true on success. Returns false on failure, e.g. if the breakpoint
  // was already inserted or there was an error while inserting it.
  virtual bool Insert() = 0;

  // Removes the breakpoint. Returns true on success, false on failure.
  virtual bool Remove() = 0;

  // Returns true if Insert() has been called successfully on this breakpoint.
  virtual bool IsInserted() const = 0;

 protected:
  uintptr_t address() const { return address_; }
  size_t kind() const { return kind_; }
  BreakpointSet* owner() const { return owner_; }

 private:
  Breakpoint() = default;

  uintptr_t address_;
  size_t kind_;
  BreakpointSet* owner_;  // weak

  FTL_DISALLOW_COPY_AND_ASSIGN(Breakpoint);
};

// Represents a software breakpoint.
class SoftwareBreakpoint final : public Breakpoint {
 public:
  SoftwareBreakpoint(uintptr_t address, size_t kind, BreakpointSet* owner);
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

  FTL_DISALLOW_COPY_AND_ASSIGN(SoftwareBreakpoint);
};

// Represents a collection of breakpoints managed by a process and defines
// operations for adding and removing them.
class BreakpointSet final {
 public:
  explicit BreakpointSet(Process* process);
  ~BreakpointSet() = default;

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

  FTL_DISALLOW_COPY_AND_ASSIGN(BreakpointSet);
};

}  // namespace arch
}  // namespace debugserver
