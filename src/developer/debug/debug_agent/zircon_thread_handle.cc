// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_thread_handle.h"

#include <map>

#include "src/developer/debug/debug_agent/process_info.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/zx_status.h"

#if defined(__x86_64__)
#include "src/developer/debug/debug_agent/arch_x64_helpers.h"
#elif defined(__aarch64__)
#include "src/developer/debug/debug_agent/arch_arm64_helpers.h"
#endif

namespace debug_agent {

ZirconThreadHandle::ZirconThreadHandle(std::shared_ptr<arch::ArchProvider> arch_provider,
                                       zx_koid_t process_koid, zx_koid_t thread_koid, zx::thread t)
    : arch_provider_(std::move(arch_provider)),
      process_koid_(process_koid),
      thread_koid_(thread_koid),
      thread_(std::move(t)) {}

uint32_t ZirconThreadHandle::GetState() const {
  zx_info_thread info;
  if (thread_.get_info(ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr) == ZX_OK)
    return info.state;
  return ZX_THREAD_STATE_DEAD;  // Assume failures mean the thread is dead.
}

zx::suspend_token ZirconThreadHandle::Suspend() {
  zx::suspend_token result;
  thread_.suspend(&result);
  return result;
}

debug_ipc::ThreadRecord ZirconThreadHandle::GetThreadRecord() const {
  debug_ipc::ThreadRecord record;
  record.process_koid = process_koid_;
  record.thread_koid = thread_koid_;

  // Name.
  char name[ZX_MAX_NAME_LEN];
  if (thread_.get_property(ZX_PROP_NAME, name, sizeof(name)) == ZX_OK)
    record.name = name;

  // State (running, blocked, etc.).
  record.state = ThreadStateToEnums(GetState(), &record.blocked_reason);

  return record;
}

std::vector<debug_ipc::Register> ZirconThreadHandle::ReadRegisters(
    const std::vector<debug_ipc::RegisterCategory>& cats_to_get) const {
  std::vector<debug_ipc::Register> regs;
  for (const auto& cat_type : cats_to_get)
    arch_provider_->ReadRegisters(cat_type, thread_, &regs);
  return regs;
}

std::vector<debug_ipc::Register> ZirconThreadHandle::WriteRegisters(
    const std::vector<debug_ipc::Register>& regs) {
  std::vector<debug_ipc::Register> written;

  // Figure out which registers will change.
  std::map<debug_ipc::RegisterCategory, std::vector<debug_ipc::Register>> categories;
  for (const debug_ipc::Register& reg : regs) {
    auto cat_type = debug_ipc::RegisterIDToCategory(reg.id);
    if (cat_type == debug_ipc::RegisterCategory::kNone) {
      FX_LOGS(WARNING) << "Attempting to change register without category: "
                       << RegisterIDToString(reg.id);
      continue;
    }

    categories[cat_type].push_back(reg);
  }

  for (const auto& [cat_type, cat_regs] : categories) {
    FX_DCHECK(cat_type != debug_ipc::RegisterCategory::kNone);
    if (auto res = arch_provider_->WriteRegisters(cat_type, cat_regs, &thread_); res != ZX_OK) {
      FX_LOGS(WARNING) << "Could not write category "
                       << debug_ipc::RegisterCategoryToString(cat_type) << ": "
                       << debug_ipc::ZxStatusToString(res);
    }

    if (auto res = arch_provider_->ReadRegisters(cat_type, thread_, &written); res != ZX_OK) {
      FX_LOGS(WARNING) << "Could not read category "
                       << debug_ipc::RegisterCategoryToString(cat_type) << ": "
                       << debug_ipc::ZxStatusToString(res);
    }
  }

  return written;
}

zx_status_t ZirconThreadHandle::InstallHWBreakpoint(uint64_t address) {
  zx_thread_state_debug_regs_t debug_regs;
  if (zx_status_t status = ReadDebugRegisters(&debug_regs); status != ZX_OK)
    return status;

  DEBUG_LOG(Archx64) << "Before installing HW breakpoint: " << std::endl
                     << arch::DebugRegistersToString(debug_regs);

  if (zx_status_t status = arch::SetupHWBreakpoint(address, &debug_regs); status != ZX_OK)
    return status;

  DEBUG_LOG(Archx64) << "After installing HW breakpoint: " << std::endl
                     << arch::DebugRegistersToString(debug_regs);

  return WriteDebugRegisters(debug_regs);
}

zx_status_t ZirconThreadHandle::UninstallHWBreakpoint(uint64_t address) {
  zx_thread_state_debug_regs_t debug_regs;
  if (zx_status_t status = ReadDebugRegisters(&debug_regs); status != ZX_OK)
    return status;

  DEBUG_LOG(Archx64) << "Before uninstalling HW breakpoint: " << std::endl
                     << arch::DebugRegistersToString(debug_regs);

  if (zx_status_t status = arch::RemoveHWBreakpoint(address, &debug_regs); status != ZX_OK)
    return status;

  DEBUG_LOG(Archx64) << "After uninstalling HW breakpoint: " << std::endl
                     << arch::DebugRegistersToString(debug_regs);

  return WriteDebugRegisters(debug_regs);
}

arch::WatchpointInstallationResult ZirconThreadHandle::InstallWatchpoint(
    debug_ipc::BreakpointType type, const debug_ipc::AddressRange& range) {
  if (!debug_ipc::IsWatchpointType(type))
    return arch::WatchpointInstallationResult(ZX_ERR_INVALID_ARGS);

  zx_thread_state_debug_regs_t debug_regs;
  if (zx_status_t status = ReadDebugRegisters(&debug_regs); status != ZX_OK)
    return arch::WatchpointInstallationResult(status);

  DEBUG_LOG(Thread) << "Before installing watchpoint for range " << range.ToString() << std::endl
                    << arch::DebugRegistersToString(debug_regs);

  auto result = arch::SetupWatchpoint(&debug_regs, type, range, arch_provider_->watchpoint_count());
  if (result.status != ZX_OK)
    return arch::WatchpointInstallationResult(result.status);

  DEBUG_LOG(Thread) << "After installing watchpoint: " << std::endl
                    << arch::DebugRegistersToString(debug_regs);

  if (zx_status_t status = WriteDebugRegisters(debug_regs); status != ZX_OK)
    return arch::WatchpointInstallationResult(status);
  return result;
}

zx_status_t ZirconThreadHandle::UninstallWatchpoint(const debug_ipc::AddressRange& range) {
  zx_thread_state_debug_regs_t debug_regs;
  if (zx_status_t status = ReadDebugRegisters(&debug_regs); status != ZX_OK)
    return status;

  DEBUG_LOG(Thread) << "Before uninstalling watchpoint: " << std::endl
                    << arch::DebugRegistersToString(debug_regs);

  // x64 doesn't support ranges.
  if (zx_status_t status =
          arch::RemoveWatchpoint(&debug_regs, range, arch_provider_->watchpoint_count());
      status != ZX_OK)
    return status;

  DEBUG_LOG(Thread) << "After uninstalling watchpoint: " << std::endl
                    << arch::DebugRegistersToString(debug_regs);

  return WriteDebugRegisters(debug_regs);
}

zx_status_t ZirconThreadHandle::ReadDebugRegisters(zx_thread_state_debug_regs* regs) const {
  return thread_.read_state(ZX_THREAD_STATE_DEBUG_REGS, regs, sizeof(zx_thread_state_debug_regs));
}

zx_status_t ZirconThreadHandle::WriteDebugRegisters(const zx_thread_state_debug_regs& regs) {
  return thread_.write_state(ZX_THREAD_STATE_DEBUG_REGS, &regs, sizeof(zx_thread_state_debug_regs));
}

}  // namespace debug_agent
