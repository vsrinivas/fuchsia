// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_TYPE_DECODER_H_
#define TOOLS_FIDLCAT_LIB_TYPE_DECODER_H_

#include <zircon/system/public/zircon/rights.h>
#include <zircon/system/public/zircon/syscalls/exception.h>
#include <zircon/system/public/zircon/syscalls/hypervisor.h>
#include <zircon/system/public/zircon/syscalls/object.h>
#include <zircon/system/public/zircon/syscalls/resource.h>
#include <zircon/system/public/zircon/types.h>

#include <cinttypes>
#include <cstdint>
#include <ostream>

#include "src/lib/fidl_codec/printer.h"
#include "zircon/system/public/zircon/syscalls/debug.h"

namespace fidlcat {

#define kOneBillion 1'000'000'000L
#define kSecondsPerMinute 60
#define kMinutesPerHour 60
#define kHoursPerDay 24

constexpr int kCharactersPerByte = 2;

// Types for syscall arguments.
enum class SyscallType {
  kBool,
  kChar,
  kCharArray,
  kInt32,
  kInt64,
  kUint8,
  kUint8Hexa,
  kUint8ArrayDecimal,
  kUint8ArrayHexa,
  kUint16,
  kUint16Hexa,
  kUint16ArrayDecimal,
  kUint16ArrayHexa,
  kUint32,
  kUint32Hexa,
  kUint32ArrayDecimal,
  kUint32ArrayHexa,
  kUint64,
  kUint64Hexa,
  kUint64ArrayDecimal,
  kUint64ArrayHexa,
  kUint128Hexa,
  kUint128ArrayHexa,
  kBtiPerm,
  kCachePolicy,
  kClock,
  kDuration,
  kExceptionChannelType,
  kExceptionState,
  kFeatureKind,
  kFutex,
  kGpAddr,
  kGuestTrap,
  kHandle,
  kInfoMapsType,
  kInterruptFlags,
  kIommuType,
  kKoid,
  kKtraceControlAction,
  kMonotonicTime,
  kObjectInfoTopic,
  kObjProps,
  kObjType,
  kPacketGuestVcpuType,
  kPacketPageRequestCommand,
  kPaddr,
  kPciBarType,
  kPolicyAction,
  kPolicyCondition,
  kPolicyTopic,
  kPortPacketType,
  kProfileInfoFlags,
  kPropType,
  kRights,
  kRsrcKind,
  kSignals,
  kSize,
  kSocketCreateOptions,
  kSocketReadOptions,
  kSocketShutdownOptions,
  kStatus,
  kSystemEventType,
  kSystemPowerctl,
  kThreadState,
  kThreadStateTopic,
  kTime,
  kTimerOption,
  kUintptr,
  kVaddr,
  kVcpu,
  kVmOption,
  kVmoCreationOption,
  kVmoOp,
  kVmoOption,
  kVmoType,
  kStruct
};

enum class SyscallReturnType {
  kNoReturn,
  kVoid,
  kStatus,
  kTicks,
  kTime,
  kUint32,
  kUint64,
};

void BtiPermName(uint32_t perm, std::ostream& os);
void CachePolicyName(uint32_t cache_policy, std::ostream& os);
void ClockName(zx_clock_t clock, std::ostream& os);
void ExceptionChannelTypeName(uint32_t type, std::ostream& os);
void ExceptionStateName(uint32_t state, std::ostream& os);
void FeatureKindName(uint32_t feature_kind, std::ostream& os);
void GuestTrapName(zx_guest_trap_t trap, std::ostream& os);
void InfoMapsTypeName(zx_info_maps_type_t type, std::ostream& os);
void InterruptFlagsName(uint32_t flags, std::ostream& os);
void IommuTypeName(uint32_t type, std::ostream& os);
void KtraceControlActionName(uint32_t action, std::ostream& os);
void ObjPropsName(zx_obj_props_t obj_props, std::ostream& os);
void PacketGuestVcpuTypeName(uint8_t type, std::ostream& os);
void PacketPageRequestCommandName(uint16_t command, std::ostream& os);
void PciBarTypeName(uint32_t type, std::ostream& os);
void PolicyActionName(uint32_t action, std::ostream& os);
void PolicyConditionName(uint32_t condition, std::ostream& os);
void PolicyTopicName(uint32_t topic, std::ostream& os);
void PortPacketTypeName(uint32_t type, std::ostream& os);
void ProfileInfoFlagsName(uint32_t flags, std::ostream& os);
void PropTypeName(uint32_t type, std::ostream& os);
void RsrcKindName(zx_rsrc_kind_t kind, std::ostream& os);
void SignalName(zx_signals_t signals, std::ostream& os);
void SocketCreateOptionsName(uint32_t options, std::ostream& os);
void SocketReadOptionsName(uint32_t options, std::ostream& os);
void SocketShutdownOptionsName(uint32_t options, std::ostream& os);
void StatusName(zx_status_t status, std::ostream& os);
void StatusName(const fidl_codec::Colors& colors, zx_status_t status, std::ostream& os);
void SystemEventTypeName(zx_system_event_type_t type, std::ostream& os);
void SystemPowerctlName(uint32_t powerctl, std::ostream& os);
void ThreadStateName(uint32_t state, std::ostream& os);
void ThreadStateTopicName(zx_thread_state_topic_t topic, std::ostream& os);
void TimerOptionName(uint32_t option, std::ostream& os);
void TopicName(uint32_t topic, std::ostream& os);
void VcpuName(uint32_t type, std::ostream& os);
void VmOptionName(zx_vm_option_t option, std::ostream& os);
void VmoCreationOptionName(uint32_t option, std::ostream& os);
void VmoOpName(uint32_t op, std::ostream& os);
void VmoOptionName(uint32_t option, std::ostream& os);
void VmoTypeName(uint32_t type, std::ostream& os);
std::string_view TypeName(SyscallType type);
void DisplayType(const fidl_codec::Colors& colors, SyscallType type, std::ostream& os);

class DisplayDuration {
 public:
  DisplayDuration(const fidl_codec::Colors& colors, zx_duration_t duration_ns)
      : colors_(colors), duration_ns_(duration_ns) {}
  [[nodiscard]] const fidl_codec::Colors& colors() const { return colors_; }
  [[nodiscard]] zx_duration_t duration_ns() const { return duration_ns_; }

 private:
  const fidl_codec::Colors& colors_;
  const zx_duration_t duration_ns_;
};

inline std::ostream& operator<<(std::ostream& os, const DisplayDuration& duration) {
  if (duration.duration_ns() == ZX_TIME_INFINITE) {
    os << duration.colors().blue << "ZX_TIME_INFINITE" << duration.colors().reset;
    return os;
  }
  if (duration.duration_ns() == ZX_TIME_INFINITE_PAST) {
    os << duration.colors().blue << "ZX_TIME_INFINITE_PAST" << duration.colors().reset;
    return os;
  }
  int64_t duration_ns = duration.duration_ns();
  os << duration.colors().blue;
  if (duration_ns < 0) {
    os << '-';
    duration_ns = -duration_ns;
  }
  const char* separator = "";
  int64_t nanoseconds = duration_ns % kOneBillion;
  int64_t seconds = duration_ns / kOneBillion;
  if (seconds != 0) {
    int64_t minutes = seconds / kSecondsPerMinute;
    if (minutes != 0) {
      int64_t hours = minutes / kMinutesPerHour;
      if (hours != 0) {
        int64_t days = hours / kHoursPerDay;
        if (days != 0) {
          os << days << " days";
          separator = ", ";
        }
        os << separator << (hours % kHoursPerDay) << " hours";
        separator = ", ";
      }
      os << separator << (minutes % kMinutesPerHour) << " minutes";
      separator = ", ";
    }
    os << separator << (seconds % kSecondsPerMinute) << " seconds";
    if (nanoseconds != 0) {
      os << " and " << nanoseconds << " nano seconds";
    }
  } else if (nanoseconds != 0) {
    os << nanoseconds << " nano seconds";
  } else {
    os << "0 seconds";
  }
  os << duration.colors().reset;
  return os;
}

class DisplayStatus {
 public:
  explicit DisplayStatus(zx_status_t status) : status_(status) {}
  [[nodiscard]] zx_status_t status() const { return status_; }

 private:
  const zx_status_t status_;
};

inline std::ostream& operator<<(std::ostream& os, const DisplayStatus& status) {
  StatusName(status.status(), os);
  return os;
}

class DisplayTime {
 public:
  DisplayTime(const fidl_codec::Colors& colors, zx_time_t time_ns)
      : colors_(colors), time_ns_(time_ns) {}
  [[nodiscard]] const fidl_codec::Colors& colors() const { return colors_; }
  [[nodiscard]] zx_time_t time_ns() const { return time_ns_; }

 private:
  const fidl_codec::Colors& colors_;
  const zx_time_t time_ns_;
};

inline std::ostream& operator<<(std::ostream& os, const DisplayTime& time) {
  if (time.time_ns() == ZX_TIME_INFINITE) {
    os << time.colors().blue << "ZX_TIME_INFINITE" << time.colors().reset;
  } else if (time.time_ns() == ZX_TIME_INFINITE_PAST) {
    os << time.colors().blue << "ZX_TIME_INFINITE_PAST" << time.colors().reset;
  } else {
    time_t value = time.time_ns() / kOneBillion;
    struct tm tm;
    if (localtime_r(&value, &tm) == &tm) {
      char buffer[100];
      strftime(buffer, sizeof(buffer), "%c", &tm);
      os << time.colors().blue << buffer << " and ";
      snprintf(buffer, sizeof(buffer), "%09" PRId64, time.time_ns() % kOneBillion);
      os << buffer << " ns" << time.colors().reset;
    } else {
      os << time.colors().red << "unknown time" << time.colors().reset;
    }
  }
  return os;
}

typedef struct {
  uint64_t low;
  uint64_t high;
} zx_uint128_t;

// This is a copy of zx_packet_guest_mem from zircon/system/public/zircon/syscalls/port.h
// specialized for AArch64.
struct zx_packet_guest_mem_aarch64 {
  zx_gpaddr_t addr;
  uint8_t access_size;
  bool sign_extend;
  uint8_t xt;
  bool read;
  uint64_t data;
  uint64_t reserved;
};
using zx_packet_guest_mem_aarch64_t = struct zx_packet_guest_mem_aarch64;

// TODO(https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=36188): Remove all
// these copies when zircon will define them.

// This is a copy of zx_packet_guest_mem from zircon/system/public/zircon/syscalls/port.h
// specialized for X86.
struct zx_packet_guest_mem_x86 {
  zx_gpaddr_t addr;
// NOTE: x86 instructions are guaranteed to be 15 bytes or fewer.
#define X86_MAX_INST_LEN 15u
  uint8_t inst_len;
  uint8_t inst_buf[X86_MAX_INST_LEN];
  uint8_t default_operand_size;
  uint8_t reserved[7];
};
using zx_packet_guest_mem_x86_t = struct zx_packet_guest_mem_x86;

// This this extracted from zx_pci_init_arg_t in the file
// zircon/system/public/zircon/syscalls/pci.h
struct zx_pci_init_arg_irq {
  uint32_t global_irq;
  bool level_triggered;
  bool active_high;
};
using zx_pci_init_arg_irq_t = struct zx_pci_init_arg_irq;

struct zx_pci_init_arg_addr_window {
  uint64_t base;
  size_t size;
  uint8_t bus_start;
  uint8_t bus_end;
  uint8_t cfg_space_type;
  bool has_ecam;
};
using zx_pci_init_arg_addr_window_t = struct zx_pci_init_arg_addr_window;

// This is a copy of zx_thread_state_general_regs_t from
// zircon/system/public/zircon/syscalls/debug.h for __aarch64__.
typedef struct zx_thread_state_general_regs_aarch64 {
  uint64_t r[30];
  uint64_t lr;
  uint64_t sp;
  uint64_t pc;
  uint64_t cpsr;
  uint64_t tpidr;
} zx_thread_state_general_regs_aarch64_t;

// This is a copy of zx_thread_state_general_regs_t from
// zircon/system/public/zircon/syscalls/debug.h for __x86_64__.
typedef struct zx_thread_state_general_regs_x86 {
  uint64_t rax;
  uint64_t rbx;
  uint64_t rcx;
  uint64_t rdx;
  uint64_t rsi;
  uint64_t rdi;
  uint64_t rbp;
  uint64_t rsp;
  uint64_t r8;
  uint64_t r9;
  uint64_t r10;
  uint64_t r11;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t rip;
  uint64_t rflags;
  uint64_t fs_base;
  uint64_t gs_base;
} zx_thread_state_general_regs_x86_t;

// This is a copy of zx_thread_state_fp_regs_t from
// zircon/system/public/zircon/syscalls/debug.h for __x86_64__.
typedef struct zx_thread_state_fp_regs_x86 {
  uint16_t fcw;  // Control word.
  uint16_t fsw;  // Status word.
  uint8_t ftw;   // Tag word.
  uint8_t reserved;
  uint16_t fop;  // Opcode.
  uint64_t fip;  // Instruction pointer.
  uint64_t fdp;  // Data pointer.

  // The x87/MMX state. For x87 the each "st" entry has the low 80 bits used for the register
  // contents. For MMX, the low 64 bits are used. The higher bits are unused.
  __ALIGNED(16)
  zx_uint128_t st[8];
} zx_thread_state_fp_regs_x86_t;

// This is a copy of zx_thread_state_vector_regs_t from
// zircon/system/public/zircon/syscalls/debug.h for __aarch64__.
typedef struct zx_thread_state_vector_regs_aarch64 {
  uint32_t fpcr;
  uint32_t fpsr;
  zx_uint128_t v[32];
} zx_thread_state_vector_regs_aarch64_t;

// This is a copy of zx_thread_state_vector_regs_t from
// zircon/system/public/zircon/syscalls/debug.h for __x86_64__
typedef struct {
  uint64_t v[8];
} zx_thread_state_vector_regs_x86_zmm_t;

typedef struct zx_thread_state_vector_regs_x86 {
  zx_thread_state_vector_regs_x86_zmm_t zmm[32];

  // AVX-512 opmask registers. Will be 0 unless AVX-512 is supported.
  uint64_t opmask[8];

  // SIMD control and status register.
  uint32_t mxcsr;
} zx_thread_state_vector_regs_x86_t;

// This is a copy of zx_thread_state_debug_regs_t from
// zircon/system/public/zircon/syscalls/debug.h for __aarch64__.

// ARMv8-A provides 2 to 16 hardware breakpoint registers.
// The number is obtained by the BRPs field in the EDDFR register.
#define AARCH64_MAX_HW_BREAKPOINTS 16
// ARMv8-A provides 2 to 16 watchpoint breakpoint registers.
// The number is obtained by the WRPs field in the EDDFR register.
#define AARCH64_MAX_HW_WATCHPOINTS 16

typedef struct {
  uint32_t dbgbcr;  //  HW Breakpoint Control register.
  uint64_t dbgbvr;  //  HW Breakpoint Value register.
} zx_thread_state_debug_regs_aarch64_bp_t;

typedef struct {
  uint32_t dbgwcr;  // HW Watchpoint Control register.
  uint64_t dbgwvr;  // HW Watchpoint Value register.
} zx_thread_state_debug_regs_aarch64_wp_t;

// Value for XZ_THREAD_STATE_DEBUG_REGS for ARM64 platforms.
typedef struct zx_thread_state_debug_regs_aarch64 {
  zx_thread_state_debug_regs_aarch64_bp_t hw_bps[AARCH64_MAX_HW_BREAKPOINTS];
  uint8_t hw_bps_count;
  zx_thread_state_debug_regs_aarch64_wp_t hw_wps[AARCH64_MAX_HW_WATCHPOINTS];
  uint8_t hw_wps_count;

  // The esr value since the last exception.
  uint32_t esr;
} zx_thread_state_debug_regs_aarch64_t;

// This is a copy of zx_thread_state_debug_regs_t from
// zircon/system/public/zircon/syscalls/debug.h for __x86_64__
typedef struct zx_thread_state_debug_regs_x86 {
  uint64_t dr[4];
  // DR4 and D5 are not used.
  uint64_t dr6;  // Status register.
  uint64_t dr7;  // Control register.
} zx_thread_state_debug_regs_x86_t;

// This is a copy of zx_vcpu_state_t from
// zircon/system/public/zircon/syscalls/hypervisor.h for __aarch64__.
typedef struct zx_vcpu_state_aarch64 {
  uint64_t x[31];
  uint64_t sp;
  uint32_t cpsr;
} zx_vcpu_state_aarch64_t;

// This is a copy of zx_vcpu_state_t from
// zircon/system/public/zircon/syscalls/hypervisor.h for __x86_64__.
typedef struct zx_vcpu_state_x86 {
  uint64_t rax;
  uint64_t rcx;
  uint64_t rdx;
  uint64_t rbx;
  uint64_t rsp;
  uint64_t rbp;
  uint64_t rsi;
  uint64_t rdi;
  uint64_t r8;
  uint64_t r9;
  uint64_t r10;
  uint64_t r11;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t rflags;
} zx_vcpu_state_x86_t;

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_TYPE_DECODER_H_
