// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_REGISTER_INFO_H_
#define SRC_DEVELOPER_DEBUG_SHARED_REGISTER_INFO_H_

#include <stdint.h>

#include <optional>
#include <string>

#include "src/developer/debug/shared/arch.h"
#include "src/developer/debug/shared/register_id.h"
#include "src/developer/debug/shared/register_value.h"
#include "src/lib/containers/cpp/array_view.h"

// Holds constant description values for all the register data for all the
// supported architectures.
// The enum definitions mirror the structs defined in the debug information
// for zircon (see zircon/system/public/zircon/syscalls/debug.h).

namespace debug {

enum class SpecialRegisterType {
  kNone,
  kIP,  // Instruction Pointer
  kSP,  // Stack Pointer
  kTP   // Thread Pointer
};

// Note that we separate out "void" addresses and "word" addresses so the debugger frontend can
// assign types to register values when appropriate.
enum class RegisterFormat {
  kGeneral,      // General register that might hold any integer or an address.
  kFloat,        // Floating-point number.
  kVector,       // Vector registers that hold multiple values.
  kVoidAddress,  // Registers that point to void*.
  kWordAddress,  // Registers that point to uint64_t.
  kSpecial,      // Things like flag registers that neither hold addresses nor numbers.
};

struct RegisterInfo {
  RegisterID id;
  std::string name;
  Arch arch;

  // Some registers refer to a subset of another register, e.g. "al" (low byte of "rax") on X86 or
  // "w0" (low 32-bits of "x0") on ARM. This ID will be the larger canonical ID. For registers that
  // are themselves canonical, this will be the same as "id".
  RegisterID canonical_id;

  // When asking for a name-to-register mapping, sometimes they map to a part of a register. For
  // example "al" on x64 is the low 8 bits of rax. These will both be 0 for the "canonical" register
  // record.
  //
  // Currently these both must be a multiple of 8 for GetRegisterData() below.
  int bits = 0;
  int shift = 0;  // How many bits shited to the right is the low bit of the value.

  // DWARF register ID if there is one.
  static constexpr uint32_t kNoDwarfId = 0xffffffff;
  uint32_t dwarf_id = kNoDwarfId;

  RegisterFormat format = RegisterFormat::kGeneral;
};

const RegisterInfo* InfoForRegister(RegisterID id);
const RegisterInfo* InfoForRegister(Arch arch, const std::string& name);

const char* RegisterIDToString(RegisterID);
RegisterID StringToRegisterID(const std::string&);

// Returns the register ID for the given special register.
RegisterID GetSpecialRegisterID(Arch, SpecialRegisterType);

// Returns the special register type for a register ID.
SpecialRegisterType GetSpecialRegisterType(RegisterID id);

// Converts the ID number used by DWARF to our register info. Returns null if not found.
const RegisterInfo* DWARFToRegisterInfo(Arch, uint32_t dwarf_reg_id);

// Find out what arch a register ID belongs to
Arch GetArchForRegisterID(RegisterID);

// Returns true if the given register is a "general" register. General
// registers are sent as part of the unwind frame data. Other registers must
// be requested specially from the target.
bool IsGeneralRegister(RegisterID);

// Gets the data for the given register from the array.
//
// This does two things. It searches for either the requested register or the canonical register.
// If it's a different canonical register (like you're asking for the a 32 bits pseudoregister out
// of a 64 bit register), the relevant bits will be extracted.
//
// If found, the return value will be the range of data within the data owned by |regs|
// corresponding to the requested register. If the source data is truncated, the result will be
// truncated also so it may have less data than expected.
//
// If the register is not found, the returned view will be empty.
containers::array_view<uint8_t> GetRegisterData(const std::vector<RegisterValue>& regs,
                                                RegisterID id);

// These ranges permit to make transformation from registerID to category and
// make some formal verifications.
constexpr uint32_t kARMv8GeneralBegin = 1000;
constexpr uint32_t kARMv8GeneralEnd = 1099;
constexpr uint32_t kARMv8VectorBegin = 1100;
constexpr uint32_t kARMv8VectorEnd = 1299;
constexpr uint32_t kARMv8DebugBegin = 1300;
constexpr uint32_t kARMv8DebugEnd = 1399;

constexpr uint32_t kX64GeneralBegin = 2000;
constexpr uint32_t kX64GeneralEnd = 2099;
constexpr uint32_t kX64FPBegin = 2100;
constexpr uint32_t kX64FPEnd = 2199;
constexpr uint32_t kX64VectorBegin = 2200;
constexpr uint32_t kX64VectorEnd = 2599;
constexpr uint32_t kX64DebugBegin = 2600;
constexpr uint32_t kX64DebugEnd = 2699;

// Categories --------------------------------------------------------------------------------------

enum class RegisterCategory : uint32_t {
  kNone = 0,
  kGeneral,
  kFloatingPoint,
  kVector,
  kDebug,

  kLast,  // Not an element, for marking the max size.
};

const char* RegisterCategoryToString(RegisterCategory);
RegisterCategory RegisterIDToCategory(RegisterID);

}  // namespace debug

#endif  // SRC_DEVELOPER_DEBUG_SHARED_REGISTER_INFO_H_
