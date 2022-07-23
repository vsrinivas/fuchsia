// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/unwinder/dwarf_expr.h"

#include <cstdint>

#include "src/developer/debug/unwinder/error.h"
#include "src/developer/debug/unwinder/memory.h"

namespace unwinder {

Error DwarfExpr::Eval(Memory* mem, const Registers& regs, uint64_t initial_value,
                      uint64_t& result) {
  if (!expr_) {
    return Error("No DWARF expression to evaluate");
  }
  std::vector<uint64_t> stack;  // the evaluation stack
  stack.push_back(initial_value);

  uint64_t p = expr_begin_;
  while (p < expr_end_) {
    uint8_t op;
    if (auto err = expr_->Read(p, op); err.has_err()) {
      return err;
    }
    switch (op) {
//
// Push const values
//
#define READ_EXPR_AND_PUSH(type)                         \
  {                                                      \
    type val;                                            \
    if (auto err = expr_->Read(p, val); err.has_err()) { \
      return err;                                        \
    }                                                    \
    stack.push_back(val);                                \
    continue;                                            \
  }
      case DW_OP_addr:
        READ_EXPR_AND_PUSH(uint64_t)
      case DW_OP_const1u:
        READ_EXPR_AND_PUSH(uint8_t)
      case DW_OP_const2u:
        READ_EXPR_AND_PUSH(uint16_t)
      case DW_OP_const4u:
        READ_EXPR_AND_PUSH(uint32_t)
      case DW_OP_const8u:
        READ_EXPR_AND_PUSH(uint64_t)
      case DW_OP_const1s:
        READ_EXPR_AND_PUSH(int8_t)
      case DW_OP_const2s:
        READ_EXPR_AND_PUSH(int16_t)
      case DW_OP_const4s:
        READ_EXPR_AND_PUSH(int32_t)
      case DW_OP_const8s:
        READ_EXPR_AND_PUSH(int64_t)
      case DW_OP_constu: {
        uint64_t val;
        if (auto err = expr_->ReadULEB128(p, val); err.has_err()) {
          return err;
        }
        stack.push_back(val);
        continue;
      }
      case DW_OP_consts: {
        int64_t val;
        if (auto err = expr_->ReadSLEB128(p, val); err.has_err()) {
          return err;
        }
        stack.push_back(val);
        continue;
      }
#undef READ_EXPR_AND_PUSH

//
// Stack operations
//
#define VALIDATE_STATE(cond) \
  if (!(cond))               \
  return Error("invalid DWARF expression")

      case DW_OP_dup: {
        VALIDATE_STATE(!stack.empty());
        stack.push_back(stack.back());
        continue;
      }
      case DW_OP_drop: {
        VALIDATE_STATE(!stack.empty());
        stack.pop_back();
        continue;
      }
      case DW_OP_pick: {
        uint8_t idx;
        if (auto err = expr_->Read(p, idx); err.has_err()) {
          return err;
        }
        VALIDATE_STATE(stack.size() >= idx);
        stack.push_back(stack[stack.size() - 1 - idx]);
        continue;
      }
      case DW_OP_over: {
        VALIDATE_STATE(stack.size() >= 2);
        stack.push_back(stack[stack.size() - 2]);
        continue;
      }
      case DW_OP_swap: {
        VALIDATE_STATE(stack.size() >= 2);
        std::swap(stack.back(), stack[stack.size() - 2]);
        continue;
      }
      case DW_OP_deref: {
        VALIDATE_STATE(!stack.empty());
        uint64_t val;
        if (auto err = mem->Read(stack.back(), val); err.has_err()) {
          return err;
        }
        stack.back() = val;
        continue;
      }

//
// Binary operations
//
#define HANDLE_BINARY_OPERATOR(op)      \
  {                                     \
    VALIDATE_STATE(stack.size() >= 2);  \
    uint64_t val = stack.back();        \
    stack.pop_back();                   \
    stack.back() = stack.back() op val; \
    continue;                           \
  }
      case DW_OP_le:
        HANDLE_BINARY_OPERATOR(<=)
      case DW_OP_ge:
        HANDLE_BINARY_OPERATOR(>=)
      case DW_OP_eq:
        HANDLE_BINARY_OPERATOR(==)
      case DW_OP_lt:
        HANDLE_BINARY_OPERATOR(<)
      case DW_OP_gt:
        HANDLE_BINARY_OPERATOR(>)
      case DW_OP_ne:
        HANDLE_BINARY_OPERATOR(!=)
      case DW_OP_and:
        HANDLE_BINARY_OPERATOR(&)
      case DW_OP_or:
        HANDLE_BINARY_OPERATOR(|)
      case DW_OP_xor:
        HANDLE_BINARY_OPERATOR(^)
      case DW_OP_plus:
        HANDLE_BINARY_OPERATOR(+)
      case DW_OP_minus:
        HANDLE_BINARY_OPERATOR(-)
      case DW_OP_mul:
        HANDLE_BINARY_OPERATOR(*)
      case DW_OP_div:
        HANDLE_BINARY_OPERATOR(/)
      case DW_OP_mod:
        HANDLE_BINARY_OPERATOR(%)
#undef HANDLE_BINARY_OPERATOR

      // Control flows.
      case DW_OP_skip: {
        int16_t skip;
        if (auto err = expr_->Read(p, skip); err.has_err()) {
          return err;
        }
        p += skip;
        continue;
      }
      case DW_OP_bra: {
        VALIDATE_STATE(!stack.empty());
        int16_t skip;
        if (auto err = expr_->Read(p, skip); err.has_err()) {
          return err;
        }
        if (stack.back()) {
          p += skip;
        }
        stack.pop_back();
        continue;
      }

      // Others.
      case DW_OP_nop: {
        continue;
      }
      case DW_OP_plus_uconst: {
        VALIDATE_STATE(!stack.empty());
        uint64_t val;
        if (auto err = expr_->ReadULEB128(p, val); err.has_err()) {
          return err;
        }
        stack.back() += val;
        continue;
      }
    }  // end of the giant switch.

    if (op >= DW_OP_lit0 && op <= DW_OP_lit31) {
      stack.push_back(op - DW_OP_lit0);
      continue;
    }
    if ((op >= DW_OP_breg0 && op <= DW_OP_breg31) || op == DW_OP_bregx) {
      RegisterID reg_id;
      if (op == DW_OP_bregx) {
        uint64_t reg;
        if (auto err = expr_->ReadULEB128(p, reg); err.has_err()) {
          return err;
        }
        reg_id = static_cast<RegisterID>(reg);
      } else {
        reg_id = static_cast<RegisterID>(op - DW_OP_breg0);
      }
      int64_t offset;
      if (auto err = expr_->ReadSLEB128(p, offset); err.has_err()) {
        return err;
      }
      uint64_t val;
      if (auto err = regs.Get(reg_id, val); err.has_err()) {
        return err;
      }
      stack.push_back(val + offset);
      continue;
    }
    return Error("unsupported DWARF expression op: %hhu", op);
  }

  VALIDATE_STATE(p == expr_end_);
  VALIDATE_STATE(!stack.empty());
#undef VALIDATE_STATE

  result = stack.back();
  return Success();
}

}  // namespace unwinder
