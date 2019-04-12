// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/process_breakpoint.h"

#include <inttypes.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>

#include "src/developer/debug/debug_agent/breakpoint.h"
#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/hardware_breakpoint.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

// Low-level Declarations ------------------------------------------------------
// Implementations are at the end of the file.

class ProcessBreakpoint::SoftwareBreakpoint {
 public:
  SoftwareBreakpoint(ProcessBreakpoint*, ProcessMemoryAccessor*);
  ~SoftwareBreakpoint();

  zx_status_t Install();
  void Uninstall();

  void FixupMemoryBlock(debug_ipc::MemoryBlock* block);

 private:
  ProcessBreakpoint* process_bp_;           // Not-owning.
  ProcessMemoryAccessor* memory_accessor_;  // Not-owning.

  // Set to true when the instruction has been replaced.
  bool installed_ = false;

  // Previous memory contents before being replaced with the break instruction.
  arch::BreakInstructionType previous_data_ = 0;
};

namespace {

// A given set of breakpoints have a number of locations, which could target
// different threads. We need to get all the threads that are targeted to
// this particular location.
std::set<zx_koid_t> HWThreadsTargeted(const ProcessBreakpoint& pb) {
  std::set<zx_koid_t> ids;
  bool all_threads = false;
  for (Breakpoint* bp : pb.breakpoints()) {
    // We only care about hardware breakpoints.
    if (bp->settings().type != debug_ipc::BreakpointType::kHardware)
      continue;

    for (auto& location : bp->settings().locations) {
      // We only install for locations that match this process breakpoint.
      if (location.address != pb.address())
        continue;

      auto thread_id = location.thread_koid;
      if (thread_id == 0) {
        all_threads = true;
        break;
      } else {
        ids.insert(thread_id);
      }
    }

    // No need to continue searching if a breakpoint wants all threads.
    if (all_threads)
      break;
  }

  // If all threads are required, add them all.
  if (all_threads) {
    for (DebuggedThread* thread : pb.process()->GetThreads())
      ids.insert(thread->koid());
  }

  return ids;
}

}  // namespace

// ProcessBreakpoint Implementation --------------------------------------------

ProcessBreakpoint::ProcessBreakpoint(Breakpoint* breakpoint,
                                     DebuggedProcess* process,
                                     ProcessMemoryAccessor* memory_accessor,
                                     uint64_t address)
    : process_(process), memory_accessor_(memory_accessor), address_(address) {
  breakpoints_.push_back(breakpoint);
}

ProcessBreakpoint::~ProcessBreakpoint() { Uninstall(); }

zx_status_t ProcessBreakpoint::Init() { return Update(); }

zx_status_t ProcessBreakpoint::RegisterBreakpoint(Breakpoint* breakpoint) {
  // Shouldn't get duplicates.
  FXL_DCHECK(std::find(breakpoints_.begin(), breakpoints_.end(), breakpoint) ==
             breakpoints_.end());
  breakpoints_.push_back(breakpoint);
  // Check if we need to install/uninstall a breakpoint.
  return Update();
}

bool ProcessBreakpoint::UnregisterBreakpoint(Breakpoint* breakpoint) {
  auto found = std::find(breakpoints_.begin(), breakpoints_.end(), breakpoint);
  if (found == breakpoints_.end()) {
    FXL_NOTREACHED();  // Should always be found.
  } else {
    breakpoints_.erase(found);
  }
  // Check if we need to install/uninstall a breakpoint.
  Update();
  return !breakpoints_.empty();
}

void ProcessBreakpoint::FixupMemoryBlock(debug_ipc::MemoryBlock* block) {
  if (software_breakpoint_)
    software_breakpoint_->FixupMemoryBlock(block);
}

void ProcessBreakpoint::OnHit(
    debug_ipc::BreakpointType exception_type,
    std::vector<debug_ipc::BreakpointStats>* hit_breakpoints) {
  hit_breakpoints->clear();
  for (Breakpoint* breakpoint : breakpoints_) {
    // Only care for breakpoints that match the exception type.
    if (breakpoint->settings().type != exception_type)
      continue;

    breakpoint->OnHit();
    hit_breakpoints->push_back(breakpoint->stats());
  }
}

void ProcessBreakpoint::BeginStepOver(zx_koid_t thread_koid) {
  // Shouldn't be recursively stepping over a breakpoint from the same thread.
  FXL_DCHECK(thread_step_over_.find(thread_koid) == thread_step_over_.end());

  if (!CurrentlySteppingOver()) {
    // This is the first thread to attempt to step over the breakpoint (there
    // could theoretically be more than one).
    Uninstall();
  }
  thread_step_over_[thread_koid] = StepStatus::kCurrent;
}

bool ProcessBreakpoint::BreakpointStepHasException(
    zx_koid_t thread_koid, debug_ipc::NotifyException::Type exception_type) {
  auto found_thread = thread_step_over_.find(thread_koid);
  if (found_thread == thread_step_over_.end()) {
    // Shouldn't be getting these notifications from a thread not currently
    // doing a step-over.
    FXL_NOTREACHED();
    return false;
  }
  StepStatus step_status = found_thread->second;
  thread_step_over_.erase(found_thread);

  // When the last thread is done stepping over, put the breakpoint back.
  if (step_status == StepStatus::kCurrent && !CurrentlySteppingOver())
    Update();

  // Now check if this exception was likely caused by successfully stepping
  // over the breakpoint, or something else (the stepped
  // instruction crashed or something).
  // TODO(donosoc): Handle HW breakpoint case.
  return exception_type == debug_ipc::NotifyException::Type::kSingleStep;
}

bool ProcessBreakpoint::CurrentlySteppingOver() const {
  for (const auto& pair : thread_step_over_) {
    if (pair.second == StepStatus::kCurrent)
      return true;
  }
  return false;
}

zx_status_t ProcessBreakpoint::Update() {
  // Software breakpoints remain installed as long as even one remains active,
  // regardless of which threads are targeted.
  int sw_bp_count = 0;
  for (Breakpoint* bp : breakpoints_) {
    if (bp->settings().type == debug_ipc::BreakpointType::kSoftware)
      sw_bp_count++;
  }

  if (sw_bp_count == 0 && software_breakpoint_) {
    software_breakpoint_.reset();
  } else if (sw_bp_count > 0 && !software_breakpoint_) {
    software_breakpoint_ =
        std::make_unique<SoftwareBreakpoint>(this, memory_accessor_);
    zx_status_t status = software_breakpoint_->Install();
    if (status != ZX_OK)
      return status;
  }

  // Hardware breakpoints are different. We need to remove for all the threads
  // that are not covered anymore.
  std::set<zx_koid_t> threads = HWThreadsTargeted(*this);

  if (threads.empty()) {
    hardware_breakpoint_.reset();
  } else {
    if (!hardware_breakpoint_) {
      hardware_breakpoint_ = std::make_unique<HardwareBreakpoint>(this);
    }
    return hardware_breakpoint_->Update(threads);
  }

  return ZX_OK;
}

void ProcessBreakpoint::Uninstall() {
  software_breakpoint_.reset();
  hardware_breakpoint_.reset();
}

// ProcessBreakpoint::SoftwareBreakpoint Implementation ------------------------

ProcessBreakpoint::SoftwareBreakpoint::SoftwareBreakpoint(
    ProcessBreakpoint* process_bp, ProcessMemoryAccessor* memory_accessor)
    : process_bp_(process_bp), memory_accessor_(memory_accessor) {}

ProcessBreakpoint::SoftwareBreakpoint::~SoftwareBreakpoint() { Uninstall(); }

zx_status_t ProcessBreakpoint::SoftwareBreakpoint::Install() {
  FXL_DCHECK(!installed_);

  uint64_t address = process_bp_->address();

  // Read previous instruction contents.
  size_t actual = 0;
  zx_status_t status = memory_accessor_->ReadProcessMemory(
      address, &previous_data_, sizeof(arch::BreakInstructionType), &actual);
  if (status != ZX_OK)
    return status;
  if (actual != sizeof(arch::BreakInstructionType))
    return ZX_ERR_UNAVAILABLE;

  // Replace with breakpoint instruction.
  status = memory_accessor_->WriteProcessMemory(
      address, &arch::kBreakInstruction, sizeof(arch::BreakInstructionType),
      &actual);
  if (status != ZX_OK)
    return status;
  if (actual != sizeof(arch::BreakInstructionType))
    return ZX_ERR_UNAVAILABLE;

  installed_ = true;
  return ZX_OK;
}

void ProcessBreakpoint::SoftwareBreakpoint::Uninstall() {
  if (!installed_)
    return;  // Not installed.

  uint64_t address = process_bp_->address();

  // If the breakpoint was previously installed it means the memory address
  // was valid and writable, so we generally expect to be able to do the same
  // write to uninstall it. But it could have been unmapped during execution
  // or even remapped with something else. So verify that it's still a
  // breakpoint instruction before doing any writes.
  arch::BreakInstructionType current_contents = 0;
  size_t actual = 0;
  zx_status_t status = memory_accessor_->ReadProcessMemory(
      address, &current_contents, sizeof(arch::BreakInstructionType), &actual);
  if (status != ZX_OK || actual != sizeof(arch::BreakInstructionType))
    return;  // Probably unmapped, safe to ignore.

  if (current_contents != arch::kBreakInstruction) {
    fprintf(stderr,
            "Warning: Debug break instruction unexpectedly replaced "
            "at %" PRIX64 "\n",
            address);
    return;  // Replaced with something else, ignore.
  }

  status = memory_accessor_->WriteProcessMemory(
      address, &previous_data_, sizeof(arch::BreakInstructionType), &actual);
  if (status != ZX_OK || actual != sizeof(arch::BreakInstructionType)) {
    fprintf(stderr, "Warning: unable to remove breakpoint at %" PRIX64 ".",
            address);
  }

  installed_ = false;
}

void ProcessBreakpoint::SoftwareBreakpoint::FixupMemoryBlock(
    debug_ipc::MemoryBlock* block) {
  if (block->data.empty())
    return;  // Nothing to do.
  FXL_DCHECK(static_cast<size_t>(block->size) == block->data.size());

  size_t src_size = sizeof(arch::BreakInstructionType);
  const uint8_t* src = reinterpret_cast<uint8_t*>(&previous_data_);

  // Simple implementation to prevent boundary errors (ARM instructions are
  // 32-bits and could be hanging partially off either end of the requested
  // buffer).
  for (size_t i = 0; i < src_size; i++) {
    uint64_t dest_address = process_bp_->address() + i;
    if (dest_address >= block->address &&
        dest_address < block->address + block->size)
      block->data[dest_address - block->address] = src[i];
  }
}

bool ProcessBreakpoint::SoftwareBreakpointInstalled() const {
  return software_breakpoint_ != nullptr;
}

bool ProcessBreakpoint::HardwareBreakpointInstalled() const {
  return hardware_breakpoint_ != nullptr &&
         !hardware_breakpoint_->installed_threads().empty();
}

// ProcessBreakpoint::HardwareBreakpoint Implementation ------------------------

}  // namespace debug_agent
