// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <hypervisor/decode.h>
#include <magenta/syscalls/hypervisor.h>

static const uint32_t kMaxInstructionLength = 15;
static const uint8_t kRexRMask = 1u << 2;
static const uint8_t kRexWMask = 1u << 3;
static const uint8_t kModRMRegMask = 0b00111000;

static bool is_rex_prefix(uint8_t prefix) {
    return (prefix >> 4) == 0b0100;
}

static bool has_sib_byte(uint8_t mod_rm) {
    return (mod_rm >> 6) != 0b11 && (mod_rm & 0b111) == 0b100;
}

static uint8_t displacement_size(uint8_t mod_rm) {
    switch (mod_rm >> 6) {
    case 0b01:
        return 1;
    case 0b10:
        return 4;
    default:
        return (mod_rm & ~kModRMRegMask) == 0b00000101 ? 4 : 0;
    }
}

static uint8_t register_id(uint8_t mod_rm, bool rex_r) {
    return ((mod_rm >> 3) & 0b111) + (rex_r ? 0b1000 : 0);
}

static uint64_t* select_register(mx_guest_gpr_t* guest_gpr, uint8_t register_id) {
    // From Intel Volume 2, Section 2.1.
    switch (register_id) {
    // From Intel Volume 2, Section 2.1.5.
    case 0:
        return &guest_gpr->rax;
    case 1:
        return &guest_gpr->rcx;
    case 2:
        return &guest_gpr->rdx;
    case 3:
        return &guest_gpr->rbx;
    case 4:
        // RSP is specially handled by the VMCS.
    default:
        return NULL;
    case 5:
        return &guest_gpr->rbp;
    case 6:
        return &guest_gpr->rsi;
    case 7:
        return &guest_gpr->rdi;
    case 8:
        return &guest_gpr->r8;
    case 9:
        return &guest_gpr->r9;
    case 10:
        return &guest_gpr->r10;
    case 11:
        return &guest_gpr->r11;
    case 12:
        return &guest_gpr->r12;
    case 13:
        return &guest_gpr->r13;
    case 14:
        return &guest_gpr->r14;
    case 15:
        return &guest_gpr->r15;
    }
}

mx_status_t deconstruct_instruction(const uint8_t* inst_buf, uint32_t inst_len,
                                    uint16_t* opcode, uint8_t* mod_rm) {
    if (inst_len == 0)
        return ERR_NOT_SUPPORTED;
    switch (inst_buf[0]) {
    case 0x0f:
        if (inst_len < 3)
            return ERR_NOT_SUPPORTED;
        *opcode = *(uint16_t*)inst_buf;
        *mod_rm = inst_buf[2];
        break;
    default:
        if (inst_len < 2)
            return ERR_OUT_OF_RANGE;
        *opcode = inst_buf[0];
        *mod_rm = inst_buf[1];
        break;
    }
    return NO_ERROR;
}

mx_status_t decode_instruction(const uint8_t* inst_buf, uint32_t inst_len,
                               mx_guest_gpr_t* guest_gpr, instruction_t* inst) {
    if (inst_len == 0)
        return ERR_BAD_STATE;
    if (inst_len > kMaxInstructionLength)
        return ERR_OUT_OF_RANGE;

    // Parse REX prefix.
    //
    // From Intel Volume 2, Appendix 2.2.1.
    //
    // TODO(abdulla): Handle more prefixes.
    bool rex_r = false;
    bool rex_w = false;
    if (is_rex_prefix(inst_buf[0])) {
        rex_r = inst_buf[0] & kRexRMask;
        rex_w = inst_buf[0] & kRexWMask;
        inst_buf++;
        inst_len--;
    }

    uint16_t opcode;
    uint8_t mod_rm;
    mx_status_t status = deconstruct_instruction(inst_buf, inst_len, &opcode, &mod_rm);
    if (status != NO_ERROR)
        return status;
    if (has_sib_byte(mod_rm))
        return ERR_NOT_SUPPORTED;

    const uint8_t disp_size = displacement_size(mod_rm);
    switch (opcode) {
    // Move r to r/m.
    case 0x89:
        if (inst_len != disp_size + 2u)
            return ERR_OUT_OF_RANGE;
        inst->read = false;
        inst->mem = rex_w ? 8 : 4;
        inst->imm = 0;
        inst->reg = select_register(guest_gpr, register_id(mod_rm, rex_r));
        return inst->reg == NULL ? ERR_NOT_SUPPORTED : NO_ERROR;
    // Move r/m to r.
    case 0x8b:
        if (inst_len != disp_size + 2u)
            return ERR_OUT_OF_RANGE;
        inst->read = true;
        inst->mem = rex_w ? 8 : 4;
        inst->imm = 0;
        inst->reg = select_register(guest_gpr, register_id(mod_rm, rex_r));
        return inst->reg == NULL ? ERR_NOT_SUPPORTED : NO_ERROR;
    // Move imm to r/m.
    case 0xc7: {
        const uint8_t imm_size = 4;
        if (inst_len != disp_size + imm_size + 2u)
            return ERR_OUT_OF_RANGE;
        if ((mod_rm & kModRMRegMask) != 0)
            return ERR_INVALID_ARGS;
        inst->read = false;
        inst->mem = rex_w ? 8 : 4;
        inst->imm = 0;
        inst->reg = NULL;
        memcpy(&inst->imm, inst_buf + disp_size + 2, imm_size);
        return NO_ERROR;
    }
    // Move (8-bit) with zero-extend r/m to r.
    case 0xb60f:
        if (inst_len != disp_size + 3u)
            return ERR_OUT_OF_RANGE;
        inst->read = true;
        inst->mem = 1;
        inst->imm = 0;
        inst->reg = select_register(guest_gpr, register_id(mod_rm, rex_r));
        return inst->reg == NULL ? ERR_NOT_SUPPORTED : NO_ERROR;
    default:
        return ERR_NOT_SUPPORTED;
    }
}
