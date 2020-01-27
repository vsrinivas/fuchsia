// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/hw/debug/arm64.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/arch_arm64_helpers.h"
#include "src/developer/debug/debug_agent/arch_helpers.h"
#include "src/developer/debug/debug_agent/arch_types.h"
#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/ipc/decode_exception.h"
#include "src/developer/debug/ipc/register_desc.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {
namespace arch {

namespace {

using debug_ipc::RegisterID;

debug_ipc::Register CreateRegister(RegisterID id, uint32_t length, const void* val_ptr) {
  debug_ipc::Register reg;
  reg.id = id;
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(val_ptr);
  reg.data.assign(ptr, ptr + length);
  return reg;
}

zx_status_t ReadGeneralRegs(const zx::thread& thread, std::vector<debug_ipc::Register>* out) {
  zx_thread_state_general_regs gen_regs;
  zx_status_t status = thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &gen_regs, sizeof(gen_regs));
  if (status != ZX_OK)
    return status;

  ArchProvider::SaveGeneralRegs(gen_regs, out);
  return ZX_OK;
}

zx_status_t ReadVectorRegs(const zx::thread& thread, std::vector<debug_ipc::Register>* out) {
  zx_thread_state_vector_regs vec_regs;
  zx_status_t status = thread.read_state(ZX_THREAD_STATE_VECTOR_REGS, &vec_regs, sizeof(vec_regs));
  if (status != ZX_OK)
    return status;

  out->push_back(CreateRegister(RegisterID::kARMv8_fpcr, 4u, &vec_regs.fpcr));
  out->push_back(CreateRegister(RegisterID::kARMv8_fpsr, 4u, &vec_regs.fpsr));

  auto base = static_cast<uint32_t>(RegisterID::kARMv8_v0);
  for (size_t i = 0; i < 32; i++) {
    auto reg_id = static_cast<RegisterID>(base + i);
    out->push_back(CreateRegister(reg_id, 16u, &vec_regs.v[i]));
  }

  return ZX_OK;
}

zx_status_t ReadDebugRegs(const zx::thread& thread, std::vector<debug_ipc::Register>* out) {
  zx_thread_state_debug_regs_t debug_regs;
  zx_status_t status =
      thread.read_state(ZX_THREAD_STATE_DEBUG_REGS, &debug_regs, sizeof(debug_regs));
  if (status != ZX_OK)
    return status;

  if (debug_regs.hw_bps_count >= AARCH64_MAX_HW_BREAKPOINTS) {
    FXL_LOG(ERROR) << "Received too many HW breakpoints: " << debug_regs.hw_bps_count
                   << " (max: " << AARCH64_MAX_HW_BREAKPOINTS << ").";
    return ZX_ERR_INVALID_ARGS;
  }

  // TODO(bug 40992) Add ARM64 hardware watchpoint registers here.

  auto bcr_base = static_cast<uint32_t>(RegisterID::kARMv8_dbgbcr0_el1);
  auto bvr_base = static_cast<uint32_t>(RegisterID::kARMv8_dbgbvr0_el1);
  for (size_t i = 0; i < debug_regs.hw_bps_count; i++) {
    auto bcr_id = static_cast<RegisterID>(bcr_base + i);
    out->push_back(
        CreateRegister(bcr_id, sizeof(debug_regs.hw_bps[i].dbgbcr), &debug_regs.hw_bps[i].dbgbcr));

    auto bvr_id = static_cast<RegisterID>(bvr_base + i);
    out->push_back(
        CreateRegister(bvr_id, sizeof(debug_regs.hw_bps[i].dbgbvr), &debug_regs.hw_bps[i].dbgbvr));
  }

  // TODO(donosoc): Currently this registers that are platform information are
  //                being hacked out as HW breakpoint values in order to know
  //                what the actual settings are.
  //                This should be changed to get the actual values instead, but
  //                check in for now in order to continue.
  out->push_back(CreateRegister(RegisterID::kARMv8_id_aa64dfr0_el1, 8u,
                                &debug_regs.hw_bps[AARCH64_MAX_HW_BREAKPOINTS - 1].dbgbvr));
  out->push_back(CreateRegister(RegisterID::kARMv8_mdscr_el1, 8u,
                                &debug_regs.hw_bps[AARCH64_MAX_HW_BREAKPOINTS - 2].dbgbvr));

  return ZX_OK;
}

class ExceptionInfo : public debug_ipc::Arm64ExceptionInfo {
 public:
  ExceptionInfo(const DebuggedThread& thread) : thread_(thread) {}

  std::optional<uint32_t> FetchESR() override {
    zx_thread_state_debug_regs_t debug_regs;
    zx_status_t status = thread_.handle().read_state(ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                                                     sizeof(zx_thread_state_debug_regs_t));
    if (status != ZX_OK) {
      DEBUG_LOG(ArchArm64) << "Could not get ESR: " << zx_status_get_string(status);
      return std::nullopt;
    }

    return debug_regs.esr;
  }

 private:
  const DebuggedThread& thread_;
};

}  // namespace

// "BRK 0" instruction.
// - Low 5 bits = 0.
// - High 11 bits = 11010100001
// - In between 16 bits is the argument to the BRK instruction (in this case
//   zero).
const BreakInstructionType kBreakInstruction = 0xd4200000;

uint64_t ArchProvider::BreakpointInstructionForSoftwareExceptionAddress(uint64_t exception_addr) {
  // ARM reports the exception for the exception instruction itself.
  return exception_addr;
}

uint64_t ArchProvider::NextInstructionForSoftwareExceptionAddress(uint64_t exception_addr) {
  // For software exceptions, the exception address is the one that caused it,
  // so next one is just 4 bytes following.
  //
  // TODO(brettw) handle THUMB. When a software breakpoint is hit, ESR_EL1
  // will contain the "instruction length" field which for T32 instructions
  // will be 0 (indicating 16-bits). This exception state somehow needs to be
  // plumbed down to our exception handler.
  return exception_addr + 4;
}

uint64_t ArchProvider::NextInstructionForWatchpointHit(uint64_t) {
  FXL_NOTREACHED() << "Not implemented.";
  return 0;
}

std::pair<debug_ipc::AddressRange, int> ArchProvider::InstructionForWatchpointHit(
    const DebuggedThread& thread) const {
  zx_thread_state_debug_regs_t debug_regs;
  if (zx_status_t status = ReadDebugState(thread.handle(), &debug_regs); status != ZX_OK) {
    DEBUG_LOG(ArchArm64) << "Could not read debug state: " << zx_status_get_string(status);
    return {{}, -1};
  }

  DEBUG_LOG(ArchArm64) << "Got FAR: 0x" << std::hex << debug_regs.far;

  // Get the closest watchpoint.
  uint64_t min_distance = UINT64_MAX;
  int closest_index = -1;
  debug_ipc::AddressRange closest_range = {};
  for (uint32_t i = 0; i < watchpoint_count(); i++) {
    uint64_t dbgwcr = debug_regs.hw_wps[i].dbgwcr;
    uint64_t dbgwvr = debug_regs.hw_wps[i].dbgwvr;  // The actual watchpoint address.

    DEBUG_LOG(ArchArm64) << "DBGWCR " << i << ": 0x" << std::hex << dbgwcr;

    if (!ARM64_DBGWCR_E_GET(dbgwcr))
      continue;

    uint32_t length = GetWatchpointLength(dbgwcr);
    if (length == 0)
      continue;

    const debug_ipc::AddressRange wp_range = {dbgwvr, dbgwvr + length};
    if (wp_range.InRange(debug_regs.far))
      return {wp_range, i};

    // Otherwise find the distance and then decide on the closest one.
    uint64_t distance = UINT64_MAX;
    if (debug_regs.far < wp_range.begin()) {
      distance = wp_range.begin() - debug_regs.far;
    } else if (debug_regs.far >= wp_range.end()) {
      distance = debug_regs.far - wp_range.end();
    } else {
      FXL_NOTREACHED() << "Invalid far/range combo. FAR: 0x" << std::hex << debug_regs.far
                       << ", range: " << wp_range.begin() << ", " << wp_range.end();
    }

    if (distance < min_distance) {
      min_distance = distance;
      closest_index = i;
      closest_range = wp_range;
    }
  }

  return {closest_range, closest_index};
}

bool ArchProvider::IsBreakpointInstruction(zx::process& process, uint64_t address) {
  BreakInstructionType data;
  size_t actual_read = 0;
  if (process.read_memory(address, &data, sizeof(BreakInstructionType), &actual_read) != ZX_OK ||
      actual_read != sizeof(BreakInstructionType))
    return false;

  // The BRK instruction could have any number associated with it, even though
  // we only write "BRK 0", so check for the low 5 and high 11 bytes as
  // described above.
  constexpr BreakInstructionType kMask = 0b11111111111000000000000000011111;
  return (data & kMask) == kBreakInstruction;
}

void ArchProvider::SaveGeneralRegs(const zx_thread_state_general_regs& input,
                                   std::vector<debug_ipc::Register>* out) {
  // Add the X0-X29 registers.
  uint32_t base = static_cast<uint32_t>(RegisterID::kARMv8_x0);
  for (int i = 0; i < 30; i++) {
    RegisterID type = static_cast<RegisterID>(base + i);
    out->push_back(CreateRegister(type, 8u, &input.r[i]));
  }

  // Add the named ones.
  out->push_back(CreateRegister(RegisterID::kARMv8_lr, 8u, &input.lr));
  out->push_back(CreateRegister(RegisterID::kARMv8_sp, 8u, &input.sp));
  out->push_back(CreateRegister(RegisterID::kARMv8_pc, 8u, &input.pc));
  out->push_back(CreateRegister(RegisterID::kARMv8_cpsr, 8u, &input.cpsr));
  out->push_back(CreateRegister(RegisterID::kARMv8_tpidr, 8u, &input.tpidr));
}

::debug_ipc::Arch ArchProvider::GetArch() { return ::debug_ipc::Arch::kArm64; }

zx_status_t ArchProvider::ReadRegisters(const debug_ipc::RegisterCategory& cat,
                                        const zx::thread& thread,
                                        std::vector<debug_ipc::Register>* out) {
  switch (cat) {
    case debug_ipc::RegisterCategory::kGeneral:
      return ReadGeneralRegs(thread, out);
    case debug_ipc::RegisterCategory::kFloatingPoint:
      // No FP registers
      return true;
    case debug_ipc::RegisterCategory::kVector:
      return ReadVectorRegs(thread, out);
    case debug_ipc::RegisterCategory::kDebug:
      return ReadDebugRegs(thread, out);
    default:
      FXL_LOG(ERROR) << "Invalid category: " << static_cast<uint32_t>(cat);
      return ZX_ERR_INVALID_ARGS;
  }
}

zx_status_t ArchProvider::WriteRegisters(const debug_ipc::RegisterCategory& category,
                                         const std::vector<debug_ipc::Register>& registers,
                                         zx::thread* thread) {
  switch (category) {
    case debug_ipc::RegisterCategory::kGeneral: {
      zx_thread_state_general_regs_t regs;
      zx_status_t res = thread->read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
      if (res != ZX_OK)
        return res;

      // Overwrite the values.
      res = WriteGeneralRegisters(registers, &regs);
      if (res != ZX_OK)
        return res;

      return thread->write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
    }
    case debug_ipc::RegisterCategory::kFloatingPoint: {
      return ZX_ERR_INVALID_ARGS;  // No floating point registers.
    }
    case debug_ipc::RegisterCategory::kVector: {
      zx_thread_state_vector_regs_t regs;
      zx_status_t res = thread->read_state(ZX_THREAD_STATE_VECTOR_REGS, &regs, sizeof(regs));
      if (res != ZX_OK)
        return res;

      // Overwrite the values.
      res = WriteVectorRegisters(registers, &regs);
      if (res != ZX_OK)
        return res;

      return thread->write_state(ZX_THREAD_STATE_VECTOR_REGS, &regs, sizeof(regs));
    }
    case debug_ipc::RegisterCategory::kDebug: {
      zx_thread_state_debug_regs_t regs;
      zx_status_t res = thread->read_state(ZX_THREAD_STATE_DEBUG_REGS, &regs, sizeof(regs));
      if (res != ZX_OK)
        return res;

      res = WriteDebugRegisters(registers, &regs);
      if (res != ZX_OK)
        return res;

      return thread->write_state(ZX_THREAD_STATE_DEBUG_REGS, &regs, sizeof(regs));
    }
    case debug_ipc::RegisterCategory::kNone:
    case debug_ipc::RegisterCategory::kLast:
      break;
  }
  FXL_NOTREACHED();
  return ZX_ERR_INVALID_ARGS;
}

debug_ipc::ExceptionType ArchProvider::DecodeExceptionType(const DebuggedThread& thread,
                                                           uint32_t exception_type) {
  ExceptionInfo info(thread);
  return debug_ipc::DecodeException(exception_type, &info);
}

// HW Breakpoints --------------------------------------------------------------

uint64_t ArchProvider::BreakpointInstructionForHardwareExceptionAddress(uint64_t exception_addr) {
  // arm64 will return the address of the instruction *about* to be executed.
  return exception_addr;
}

zx_status_t ArchProvider::InstallHWBreakpoint(const zx::thread& thread, uint64_t address) {
  zx_thread_state_debug_regs_t debug_regs;
  if (zx_status_t status = ReadDebugState(thread, &debug_regs); status != ZX_OK)
    return status;

  DEBUG_LOG(ArchArm64) << "Before installing HW breakpoint: " << std::endl
                       << DebugRegistersToString(debug_regs);

  if (zx_status_t status = SetupHWBreakpoint(address, &debug_regs); status != ZX_OK)
    return status;

  DEBUG_LOG(ArchArm64) << "After installing HW breakpoint: " << std::endl
                       << DebugRegistersToString(debug_regs);

  return WriteDebugState(thread, debug_regs);
}

zx_status_t ArchProvider::UninstallHWBreakpoint(const zx::thread& thread, uint64_t address) {
  zx_thread_state_debug_regs_t debug_regs;
  if (zx_status_t status = ReadDebugState(thread, &debug_regs); status != ZX_OK)
    return status;

  DEBUG_LOG(ArchArm64) << "Before uninstalling HW breakpoint: " << std::endl
                       << DebugRegistersToString(debug_regs);

  if (zx_status_t status = RemoveHWBreakpoint(address, &debug_regs); status != ZX_OK)
    return status;

  DEBUG_LOG(ArchArm64) << "After uninstalling HW breakpoint: " << std::endl
                       << DebugRegistersToString(debug_regs);

  return WriteDebugState(thread, debug_regs);
}

WatchpointInstallationResult ArchProvider::InstallWatchpoint(debug_ipc::BreakpointType type,
                                                             const zx::thread& thread,
                                                             const debug_ipc::AddressRange& range) {
  if (!debug_ipc::IsWatchpointType(type))
    return WatchpointInstallationResult(ZX_ERR_INVALID_ARGS);

  zx_thread_state_debug_regs_t debug_regs;
  if (zx_status_t status = ReadDebugState(thread, &debug_regs); status != ZX_OK)
    return WatchpointInstallationResult(status);

  DEBUG_LOG(ArchArm64) << "Before installing watchpoint: " << std::endl
                       << DebugRegistersToString(debug_regs);

  WatchpointInstallationResult result =
      SetupWatchpoint(&debug_regs, type, range, watchpoint_count());
  if (result.status != ZX_OK) {
    DEBUG_LOG(ArchArm64) << "Could not install watchpoint: " << zx_status_get_string(result.status);
    return result;
  }

  DEBUG_LOG(ArchArm64) << "After installing watchpoint: " << std::endl
                       << DebugRegistersToString(debug_regs);

  if (zx_status_t status = WriteDebugState(thread, debug_regs); status != ZX_OK)
    return WatchpointInstallationResult(status);

  return result;
}

zx_status_t ArchProvider::UninstallWatchpoint(const zx::thread& thread,
                                              const debug_ipc::AddressRange& range) {
  zx_thread_state_debug_regs_t debug_regs = {};
  if (zx_status_t status = ReadDebugState(thread, &debug_regs); status != ZX_OK)
    return status;

  DEBUG_LOG(ArchArm64) << "Before uninstalling watchpoint: " << std::endl
                       << DebugRegistersToString(debug_regs);

  if (zx_status_t status = RemoveWatchpoint(&debug_regs, range, watchpoint_count());
      status != ZX_OK) {
    return status;
  }

  DEBUG_LOG(ArchArm64) << "After uninstalling watchpoint: " << std::endl
                       << DebugRegistersToString(debug_regs);

  return WriteDebugState(thread, debug_regs);
}

}  // namespace arch
}  // namespace debug_agent
