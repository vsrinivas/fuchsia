// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_HYPERVISOR_VMEXIT_PRIV_H_
#define ZIRCON_KERNEL_ARCH_ARM64_HYPERVISOR_VMEXIT_PRIV_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>
#include <hypervisor/aspace.h>
#include <hypervisor/trap_map.h>

typedef struct zx_port_packet zx_port_packet_t;

struct GuestState;
class GichState;

// clang-format off

// Exception class of an exception syndrome.
enum class ExceptionClass : uint8_t {
  WFI_WFE_INSTRUCTION = 0b000001,
  SMC_INSTRUCTION     = 0b010111,
  SYSTEM_INSTRUCTION  = 0b011000,
  INSTRUCTION_ABORT   = 0b100000,
  DATA_ABORT          = 0b100100,
  SERROR_INTERRUPT    = 0b101111,
};

static inline const char* exception_class_name(ExceptionClass exception_class) {
#define EXCEPTION_CLASS_NAME(name) case ExceptionClass::name: return #name
  switch (exception_class) {
    EXCEPTION_CLASS_NAME(WFI_WFE_INSTRUCTION);
    EXCEPTION_CLASS_NAME(SMC_INSTRUCTION);
    EXCEPTION_CLASS_NAME(SYSTEM_INSTRUCTION);
    EXCEPTION_CLASS_NAME(INSTRUCTION_ABORT);
    EXCEPTION_CLASS_NAME(DATA_ABORT);
    EXCEPTION_CLASS_NAME(SERROR_INTERRUPT);
#undef EXIT_REASON_NAME
  default:
    return "UNKNOWN";
  }
}

// Exception syndrome for a VM exit.
struct ExceptionSyndrome {
  ExceptionClass ec;
  uint32_t iss;

  explicit ExceptionSyndrome(uint32_t esr);
};

// Wait instruction that caused a VM exit.
struct WaitInstruction {
  bool is_wfe;

  explicit WaitInstruction(uint32_t iss);
};

// SMC instruction that cause a VM exit.
struct SmcInstruction {
  uint16_t imm;

  explicit SmcInstruction(uint32_t iss);
};

// System register associated with a system instruction.
enum class SystemRegister : uint16_t {
  MAIR_EL1        = 0b11000000 << 8 /* op */ | 0b10100010 /* cr */,
  SCTLR_EL1       = 0b11000000 << 8 /* op */ | 0b00010000 /* cr */,
  TCR_EL1         = 0b11010000 << 8 /* op */ | 0b00100000 /* cr */,
  TTBR0_EL1       = 0b11000000 << 8 /* op */ | 0b00100000 /* cr */,
  TTBR1_EL1       = 0b11001000 << 8 /* op */ | 0b00100000 /* cr */,

  // Debug Registers, trapped by MDCR_EL2.TDOSA = 1
  OSLAR_EL1       = 0b10100000 << 8 /* op */ | 0b00010000 /* cr */,
  OSLSR_EL1       = 0b10100000 << 8 /* op */ | 0b00010001 /* cr */,
  OSDLR_EL1       = 0b10100000 << 8 /* op */ | 0b00010011 /* cr */,
  DBGPRCR_EL1     = 0b10100000 << 8 /* op */ | 0b00010100 /* cr */,

  // Interrupt Controller System Registers. See GIC v3/v4 Architecture Spec Section 8.2.
  ICC_SGI1R_EL1   = 0b11101000 << 8 /* op */ | 0b11001011 /* cr */,

  // Data cache operations by set/way.
  DC_ISW          = 0b01010000 << 8 /* op */ | 0b01110110 /* cr */,
  DC_CISW         = 0b01010000 << 8 /* op */ | 0b01111110 /* cr */,
  DC_CSW          = 0b01010000 << 8 /* op */ | 0b01111010 /* cr */,
};

// clang-format on

// System instruction that caused a VM exit.
struct SystemInstruction {
  SystemRegister sysreg;
  uint8_t xt;
  bool read;

  explicit SystemInstruction(uint32_t iss);
};

struct SgiRegister {
  uint8_t aff3;
  uint8_t aff2;
  uint8_t aff1;
  uint8_t rs;
  uint16_t target_list;
  uint8_t int_id;
  bool all_but_local;

  explicit SgiRegister(uint64_t sgi);
};

// Data abort that caused a VM exit.
struct DataAbort {
  bool valid;
  uint8_t access_size;
  bool sign_extend;
  uint8_t xt;
  bool read;

  explicit DataAbort(uint32_t iss);
};

// SError interrupt that caused a VM exit.
struct SError {
  uint32_t iss;

  enum class ErrorType {
    kUncontainable = 0b000,
    kUnrecoverableState = 0b001,
    kRestartableState = 0b010,
    kRecoverableState = 0b011,
    kCorrected = 0b110,
  };

  enum class DataFaultStatusCode {
    kUncategorized = 0b000000,
    kAsyncSError = 0b010001,
  };

  DEF_SUBBIT(iss, 24, ids);  // Implementation defined syndrome
  // Bits [23:14] reserved.
  DEF_SUBBIT(iss, 13, iesb);                       // Implicit error synchronization event.
  DEF_ENUM_SUBFIELD(iss, ErrorType, 12, 10, aet);  // Asynchronous error type
  DEF_SUBBIT(iss, 9, ea);                          // External abort type.
  // Bits [8:6] reserved.
  DEF_ENUM_SUBFIELD(iss, DataFaultStatusCode, 5, 0, dfsc);  // Data fault status code

  explicit SError(uint32_t iss) : iss(iss) {}
};

void timer_maybe_interrupt(GuestState* guest_state, GichState* gich_state);
zx_status_t vmexit_handler(uint64_t* hcr, GuestState* guest_state, GichState* gich_state,
                           hypervisor::GuestPhysicalAspace* gpa, hypervisor::TrapMap* traps,
                           zx_port_packet_t* packet);

#endif  // ZIRCON_KERNEL_ARCH_ARM64_HYPERVISOR_VMEXIT_PRIV_H_
