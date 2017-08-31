// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <hypervisor/decode.h>
#include <magenta/syscalls/hypervisor.h>
#include <magenta/syscalls/port.h>

static const uint8_t kRexRMask = 1u << 2;
static const uint8_t kRexWMask = 1u << 3;
static const uint8_t kModRMRegMask = 0b00111000;

static bool is_h66_prefix(uint8_t prefix) {
    return prefix == 0x66;
}

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

static uint8_t mem_size(bool h66, bool rex_w) {
    if (rex_w) {
        return 8;
    } else if (!h66) {
        return 4;
    } else {
        return 2;
    }
}

static uint8_t register_id(uint8_t mod_rm, bool rex_r) {
    return static_cast<uint8_t>(((mod_rm >> 3) & 0b111) + (rex_r ? 0b1000 : 0));
}

static uint64_t* select_register(mx_vcpu_state_t* vcpu_state, uint8_t register_id) {
    // From Intel Volume 2, Section 2.1.
    switch (register_id) {
    // From Intel Volume 2, Section 2.1.5.
    case 0:
        return &vcpu_state->rax;
    case 1:
        return &vcpu_state->rcx;
    case 2:
        return &vcpu_state->rdx;
    case 3:
        return &vcpu_state->rbx;
    case 4:
        return &vcpu_state->rsp;
    case 5:
        return &vcpu_state->rbp;
    case 6:
        return &vcpu_state->rsi;
    case 7:
        return &vcpu_state->rdi;
    case 8:
        return &vcpu_state->r8;
    case 9:
        return &vcpu_state->r9;
    case 10:
        return &vcpu_state->r10;
    case 11:
        return &vcpu_state->r11;
    case 12:
        return &vcpu_state->r12;
    case 13:
        return &vcpu_state->r13;
    case 14:
        return &vcpu_state->r14;
    case 15:
        return &vcpu_state->r15;
    default:
        return NULL;
    }
}

mx_status_t deconstruct_instruction(const uint8_t* inst_buf, uint32_t inst_len,
                                    uint16_t* opcode, uint8_t* mod_rm) {
    if (inst_len == 0)
        return MX_ERR_NOT_SUPPORTED;
    switch (inst_buf[0]) {
    case 0x0f:
        if (inst_len < 3)
            return MX_ERR_NOT_SUPPORTED;
        *opcode = *(uint16_t*)inst_buf;
        *mod_rm = inst_buf[2];
        break;
    default:
        if (inst_len < 2)
            return MX_ERR_OUT_OF_RANGE;
        *opcode = inst_buf[0];
        *mod_rm = inst_buf[1];
        break;
    }
    return MX_OK;
}

mx_status_t inst_decode(const uint8_t* inst_buf, uint32_t inst_len, mx_vcpu_state_t* vcpu_state,
                        instruction_t* inst) {
    if (inst_len == 0)
        return MX_ERR_BAD_STATE;
    if (inst_len > X86_MAX_INST_LEN)
        return MX_ERR_OUT_OF_RANGE;

    // Parse 66H prefix.
    bool h66 = is_h66_prefix(inst_buf[0]);
    if (h66) {
        if (inst_len == 1)
            return MX_ERR_BAD_STATE;
        inst_buf++;
        inst_len--;
    }
    // Parse REX prefix.
    //
    // From Intel Volume 2, Appendix 2.2.1: Only one REX prefix is allowed per
    // instruction. If used, the REX prefix byte must immediately precede the
    // opcode byte or the escape opcode byte (0FH).
    bool rex_r = false;
    bool rex_w = false;
    if (is_rex_prefix(inst_buf[0])) {
        rex_r = inst_buf[0] & kRexRMask;
        rex_w = inst_buf[0] & kRexWMask;
        inst_buf++;
        inst_len--;
    }
    // Technically this is valid, but no sane compiler should emit it.
    if (h66 && rex_w)
        return MX_ERR_NOT_SUPPORTED;

    uint16_t opcode;
    uint8_t mod_rm;
    mx_status_t status = deconstruct_instruction(inst_buf, inst_len, &opcode, &mod_rm);
    if (status != MX_OK)
        return status;
    if (has_sib_byte(mod_rm))
        return MX_ERR_NOT_SUPPORTED;

    const uint8_t disp_size = displacement_size(mod_rm);
    switch (opcode) {
    // Move r to r/m.
    case 0x89:
        if (inst_len != disp_size + 2u)
            return MX_ERR_OUT_OF_RANGE;
        inst->type = INST_MOV_WRITE;
        inst->mem = mem_size(h66, rex_w);
        inst->imm = 0;
        inst->reg = select_register(vcpu_state, register_id(mod_rm, rex_r));
        inst->flags = NULL;
        return inst->reg == NULL ? MX_ERR_NOT_SUPPORTED : MX_OK;
    // Move r/m to r.
    case 0x8b:
        if (inst_len != disp_size + 2u)
            return MX_ERR_OUT_OF_RANGE;
        inst->type = INST_MOV_READ;
        inst->mem = mem_size(h66, rex_w);
        inst->imm = 0;
        inst->reg = select_register(vcpu_state, register_id(mod_rm, rex_r));
        inst->flags = NULL;
        return inst->reg == NULL ? MX_ERR_NOT_SUPPORTED : MX_OK;
    // Move imm to r/m.
    case 0xc7: {
        const uint8_t imm_size = h66 ? 2 : 4;
        if (inst_len != disp_size + imm_size + 2u)
            return MX_ERR_OUT_OF_RANGE;
        if ((mod_rm & kModRMRegMask) != 0)
            return MX_ERR_INVALID_ARGS;
        inst->type = INST_MOV_WRITE;
        inst->mem = mem_size(h66, rex_w);
        inst->imm = 0;
        inst->reg = NULL;
        inst->flags = NULL;
        memcpy(&inst->imm, inst_buf + disp_size + 2, imm_size);
        return MX_OK;
    }
    // Move (8-bit) with zero-extend r/m to r.
    case 0xb60f:
        if (h66)
            return MX_ERR_BAD_STATE;
        if (inst_len != disp_size + 3u)
            return MX_ERR_OUT_OF_RANGE;
        inst->type = INST_MOV_READ;
        inst->mem = 1;
        inst->imm = 0;
        inst->reg = select_register(vcpu_state, register_id(mod_rm, rex_r));
        inst->flags = NULL;
        return inst->reg == NULL ? MX_ERR_NOT_SUPPORTED : MX_OK;
    // Move (16-bit) with zero-extend r/m to r.
    case 0xb70f:
        if (h66)
            return MX_ERR_BAD_STATE;
        if (inst_len != disp_size + 3u)
            return MX_ERR_OUT_OF_RANGE;
        inst->type = INST_MOV_READ;
        inst->mem = 2;
        inst->imm = 0;
        inst->reg = select_register(vcpu_state, register_id(mod_rm, rex_r));
        inst->flags = NULL;
        return inst->reg == NULL ? MX_ERR_NOT_SUPPORTED : MX_OK;
    // Logical compare (8-bit) imm with r/m.
    case 0xf6:
        if (h66)
            return MX_ERR_BAD_STATE;
        if (inst_len != disp_size + 3u)
            return MX_ERR_OUT_OF_RANGE;
        if ((mod_rm & kModRMRegMask) != 0)
            return MX_ERR_INVALID_ARGS;
        inst->type = INST_TEST;
        inst->mem = 1;
        inst->imm = 0;
        inst->reg = NULL;
        inst->flags = &vcpu_state->flags;
        memcpy(&inst->imm, inst_buf + disp_size + 2, 1);
        return MX_OK;
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}
