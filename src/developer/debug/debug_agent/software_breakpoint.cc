// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/software_breakpoint.h"

#include <inttypes.h>
#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/debug_agent/breakpoint.h"
#include "src/developer/debug/debug_agent/process_breakpoint.h"
#include "src/developer/debug/debug_agent/process_handle.h"
#include "src/developer/debug/shared/logging/logging.h"

namespace debug_agent {

namespace {

std::string LogPreamble(ProcessBreakpoint* b) {
  std::stringstream ss;

  ss << "[SW BP 0x" << std::hex << b->address();
  bool first = true;

  // Add the names of all the breakpoints associated with this process breakpoint.
  ss << " (";
  for (Breakpoint* breakpoint : b->breakpoints()) {
    if (!first) {
      first = false;
      ss << ", ";
    }
    ss << breakpoint->settings().name;
  }

  ss << ")] ";
  return ss.str();
}

}  // namespace

SoftwareBreakpoint::SoftwareBreakpoint(Breakpoint* breakpoint, DebuggedProcess* process,
                                       uint64_t address)
    : ProcessBreakpoint(breakpoint, process, address) {}

SoftwareBreakpoint::~SoftwareBreakpoint() { Uninstall(); }

debug::Status SoftwareBreakpoint::Update() {
  // Software breakpoints remain installed as long as even one remains active, regardless of which
  // threads are targeted.
  int sw_bp_count = 0;
  for (Breakpoint* bp : breakpoints()) {
    if (bp->settings().type == debug_ipc::BreakpointType::kSoftware)
      sw_bp_count++;
  }

  if (sw_bp_count == 0 && installed_) {
    Uninstall();
  } else if (sw_bp_count > 0 && !installed_) {
    return Install();
  }

  return debug::Status();
}

debug::Status SoftwareBreakpoint::Install() {
  FX_DCHECK(!installed_);

  // Read previous instruction contents.
  size_t actual = 0;
  if (debug::Status status = process_->process_handle().ReadMemory(
          address(), &previous_data_, arch::kBreakInstructionSize, &actual);
      status.has_error())
    return status;
  if (actual != arch::kBreakInstructionSize)
    return debug::Status("Could not read breakpoint memory.");

  // Replace with breakpoint instruction.
  if (debug::Status status = process_->process_handle().WriteMemory(
          address(), &arch::kBreakInstruction, arch::kBreakInstructionSize, &actual);
      status.has_error())
    return status;
  if (actual != arch::kBreakInstructionSize)
    return debug::Status("Could not write breakpoint memory.");

  installed_ = true;
  return debug::Status();
}

debug::Status SoftwareBreakpoint::Uninstall() {
  if (!installed_)
    return debug::Status();  // Not installed.

  // If the breakpoint was previously installed it means the memory address was valid and writable,
  // so we generally expect to be able to do the same write to uninstall it. But it could have been
  // unmapped during execution or even remapped with something else. So verify that it's still a
  // breakpoint instruction before doing any writes.
  arch::BreakInstructionType current_contents = 0;
  size_t actual = 0;
  debug::Status status = process_->process_handle().ReadMemory(
      address(), &current_contents, arch::kBreakInstructionSize, &actual);
  if (status.has_error() || actual != arch::kBreakInstructionSize)
    return debug::Status();  // Probably unmapped, safe to ignore.

  if (current_contents != arch::kBreakInstruction) {
    FX_LOGS(WARNING) << "Debug break instruction unexpectedly replaced at 0x" << std::hex
                     << address();
    return debug::Status();  // Replaced with something else, ignore.
  }

  status = process_->process_handle().WriteMemory(address(), &previous_data_,
                                                  arch::kBreakInstructionSize, &actual);
  if (status.has_error() || actual != arch::kBreakInstructionSize) {
    FX_LOGS(WARNING) << "Unable to remove breakpoint at 0x" << std::hex << address();
  }

  installed_ = false;
  return debug::Status();
}

void SoftwareBreakpoint::FixupMemoryBlock(debug_ipc::MemoryBlock* block) {
  if (block->data.empty())
    return;  // Nothing to do.
  FX_DCHECK(static_cast<size_t>(block->size) == block->data.size());

  size_t src_size = arch::kBreakInstructionSize;
  const uint8_t* src = reinterpret_cast<uint8_t*>(&previous_data_);

  // Simple implementation to prevent boundary errors (ARM instructions are 32-bits and could be
  // hanging partially off either end of the requested buffer).
  for (size_t i = 0; i < src_size; i++) {
    uint64_t dest_address = address() + i;
    if (dest_address >= block->address && dest_address < block->address + block->size)
      block->data[dest_address - block->address] = src[i];
  }
}

void SoftwareBreakpoint::ExecuteStepOver(DebuggedThread* thread) {
  DEBUG_LOG(Breakpoint) << LogPreamble(this) << "Thread " << thread->koid() << " is stepping over.";
  currently_stepping_over_thread_ = thread->GetWeakPtr();
  thread->set_stepping_over_breakpoint(true);

  SuspendAllOtherThreads(thread->koid());

  Uninstall(thread);

  // This thread now has to continue running.
  thread->InternalResumeException();
}

void SoftwareBreakpoint::EndStepOver(DebuggedThread* thread) {
  FX_DCHECK(thread->stepping_over_breakpoint());
  FX_DCHECK(currently_stepping_over_thread_);
  FX_DCHECK(currently_stepping_over_thread_->koid() == thread->koid())
      << " Expected " << currently_stepping_over_thread_->koid() << ", Got " << thread->koid();

  DEBUG_LOG(Breakpoint) << LogPreamble(this) << "Thread " << thread->koid() << " ending step over.";
  thread->set_stepping_over_breakpoint(false);
  currently_stepping_over_thread_.reset();

  // Install the breakpoint again.
  // NOTE(donosoc): For multiple threads stepping over (queue), this is inefficient as threads are
  //                suspended and there is no need to reinstall them every time, expect for
  //                implementation simplicity. If performance becomes an issue, we could create a
  //                notification that the process calls when the complete step queue has been done
  //                that tells the breakpoints to reinstall themselves.
  Update();

  // Tell the process we're done stepping over.
  process_->OnBreakpointFinishedSteppingOver();
}

void SoftwareBreakpoint::StepOverCleanup(DebuggedThread* thread) {
  DEBUG_LOG(Breakpoint) << LogPreamble(this) << "Finishing step over for thread " << thread->koid();

  // We are done stepping over this thread, so we can remove the suspend tokens. Normally this means
  // cleaning all the suspend tokens, if there is only one thread in the stepping over queue or the
  // next step over is another breakpoint.
  //
  // But in the case that another thread is stepping over *the same* breakpoint, cleaning all the
  // tokens would resume all the threads that have just been suspended by the next instance of the
  // step over.
  //
  // For this case we need the ability to maintain more than one suspend tokens per thread: one for
  // the first step over and another for the second, as they coincide between the process callind
  // |ExecuteStepOver| on the second instance and callind |StepOverCleanup| on the first one.
  auto it = suspend_tokens_.begin();
  while (it != suspend_tokens_.end()) {
    // Calculate the upper bound (so we skip over repeated keys).
    auto cur_it = it;
    it = suspend_tokens_.upper_bound(it->first);

    // We do not erase a token for the thread we just stepped over, because it will be the only
    // thread that will not have 2 suspend tokens: It will have the one taken by the next step over,
    // as the first one didn't get one.
    if (cur_it->first == thread->koid())
      continue;

    // All other threads would have 2 suspend tokens (one for the first step over and one for the
    // second), meaning that we can safely remove the first one.
    suspend_tokens_.erase(cur_it);
  }

  // Remote the thread from the exception.
  thread->InternalResumeException();
}

void SoftwareBreakpoint::SuspendAllOtherThreads(zx_koid_t stepping_over_koid) {
  std::vector<DebuggedThread*> suspended_threads;
  for (DebuggedThread* thread : process_->GetThreads()) {
    // We do not suspend the stepping over thread.
    if (thread->koid() == stepping_over_koid)
      continue;

    // Only one thread should be stepping over at a time.
    if (thread->stepping_over_breakpoint() && thread->koid() != stepping_over_koid) {
      FX_NOTREACHED() << "Thread " << thread->koid() << " is stepping over. Only thread "
                      << stepping_over_koid << " should be stepping over.";
    }

    // We keep every other thread suspended.
    // If this is a re-entrant breakpoint (two threads in a row are stepping over the same
    // breakpoint), we could have more than one token for each thread.
    suspend_tokens_.insert({thread->koid(), thread->InternalSuspend(false)});
  }

  // We wait on all the suspend signals to trigger.
  for (DebuggedThread* thread : suspended_threads) {
    bool suspended =
        thread->thread_handle().WaitForSuspension(DebuggedThread::DefaultSuspendDeadline());
    FX_DCHECK(suspended) << "Thread " << thread->koid();
  }
}

std::vector<zx_koid_t> SoftwareBreakpoint::CurrentlySuspendedThreads() const {
  std::vector<zx_koid_t> koids;
  koids.reserve(suspend_tokens_.size());
  for (auto& [thread_koid, _] : suspend_tokens_) {
    koids.emplace_back(thread_koid);
  }

  std::sort(koids.begin(), koids.end());
  return koids;
}

}  // namespace debug_agent
