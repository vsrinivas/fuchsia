// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file exists merely to offload logics from dwarf_cfi.h to avoid the file being too large.

#ifndef SRC_DEVELOPER_DEBUG_UNWINDER_DWARF_CFI_PARSER_H_
#define SRC_DEVELOPER_DEBUG_UNWINDER_DWARF_CFI_PARSER_H_

#include <cstdint>
#include <vector>

#include "src/developer/debug/unwinder/dwarf_expr.h"
#include "src/developer/debug/unwinder/memory.h"
#include "src/developer/debug/unwinder/registers.h"

namespace unwinder {

// Parse the call frame instructions to get the locations of CFA and registers.
class DwarfCfiParser {
 public:
  // arch is needed to default initialize register_locations_.
  DwarfCfiParser(Registers::Arch arch, uint64_t code_alignment_factor,
                 int64_t data_alignment_factor);

  // Parse the CFA instructions until the (relative) pc reaches pc_limit.
  [[nodiscard]] Error ParseInstructions(Memory* elf, uint64_t instructions_begin,
                                        uint64_t instructions_end, uint64_t pc_limit);

  // Helper for DW_CFA_restore. This function should be called after CIE instructions are parsed but
  // before the FDE instructions are parsed.
  void Snapshot() { initial_register_locations_ = register_locations_; }

  // Apply the frame info to unwind one frame.
  [[nodiscard]] Error Step(Memory* stack, RegisterID return_address_register,
                           const Registers& current, Registers& next);

 private:
  struct RegisterLocation {
    enum class Type {
      kUndefined,   // register is scratched, i.e. DW_CFA_undefined.
      kSameValue,   // register is preserved, i.e. DW_CFA_same_value.
      kRegister,    // register is stored in another register, i.e. DW_CFA_register.
      kOffset,      // register is saved relative to the CFA with an offset, i.e. DW_CFA_offset.
      kExpression,  // register is saved at an address that can be calculated by a DWARF expression.
      kValExpression,  // register's value can be calculated by a DWARF expression.
    } type = Type::kUndefined;

    // The ID of the other register. Only valid when type is kRegister.
    RegisterID reg_id;

    // Only valid when type is kOffset.
    int64_t offset;

    // Only valid when type is kExpression or kValExpression.
    DwarfExpr expression;
  };

  const uint64_t code_alignment_factor_;
  const int64_t data_alignment_factor_;

  struct CfaLocation {
    RegisterID reg = RegisterID::kInvalid;
    uint64_t offset = -1;
  } cfa_location_;

  using RegisterLocations = std::map<RegisterID, RegisterLocation>;
  RegisterLocations register_locations_;

  // Copy of register_locations_ for DW_CFA_restore.
  RegisterLocations initial_register_locations_;

  // Stack of states for DW_CFA_remember_state and DW_CFA_restore_state.
  std::vector<std::pair<CfaLocation, RegisterLocations>> state_stack_;
};

}  // namespace unwinder

#endif  // SRC_DEVELOPER_DEBUG_UNWINDER_DWARF_CFI_PARSER_H_
