// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/decode.h>
#include <magenta/errors.h>
#include <magenta/syscalls/hypervisor.h>
#include <unittest/unittest.h>

static bool decode_failure(void) {
    BEGIN_TEST;

    EXPECT_EQ(decode_instruction(NULL, 0, NULL, NULL), ERR_BAD_STATE, "");
    EXPECT_EQ(decode_instruction(NULL, 32, NULL, NULL), ERR_OUT_OF_RANGE, "");

    uint8_t bad_rex[] = { 0b0100u << 4, 0, 0 };
    EXPECT_EQ(decode_instruction(bad_rex, 1, NULL, NULL), ERR_NOT_SUPPORTED, "");
    EXPECT_EQ(decode_instruction(bad_rex, 2, NULL, NULL), ERR_OUT_OF_RANGE, "");
    EXPECT_EQ(decode_instruction(bad_rex, 3, NULL, NULL), ERR_NOT_SUPPORTED, "");

    uint8_t bad_len[] = { 0, 0 };
    EXPECT_EQ(decode_instruction(bad_len, 2, NULL, NULL), ERR_NOT_SUPPORTED, "");

    END_TEST;
}

static bool decode_mov_89(void) {
    BEGIN_TEST;

    uint8_t bad_len[] = { 0x89, 0, 0 };
    EXPECT_EQ(decode_instruction(bad_len, 3, NULL, NULL), ERR_OUT_OF_RANGE, "");
    uint8_t bad_disp[] = { 0x89, 0b01000000 };
    EXPECT_EQ(decode_instruction(bad_disp, 2, NULL, NULL), ERR_OUT_OF_RANGE, "");
    uint8_t has_sib[] = { 0x89, 0b01000100, 0, 0 };
    EXPECT_EQ(decode_instruction(has_sib, 4, NULL, NULL), ERR_NOT_SUPPORTED, "");

    // mov %ecx, (%rax)
    uint8_t mov[] = { 0x89, 0b00001000 };
    mx_guest_gpr_t guest_gpr;
    instruction_t inst;
    EXPECT_EQ(decode_instruction(mov, 2, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_EQ(inst.mem, 4u, "");
    EXPECT_EQ(inst.imm, 0u, "");
    EXPECT_EQ(inst.reg, &guest_gpr.rcx, "");

    // mov %r10d, (%rax)
    uint8_t rex_mov[] = { 0b01000100, 0x89, 0b00010000 };
    EXPECT_EQ(decode_instruction(rex_mov, 3, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_EQ(inst.mem, 4u, "");
    EXPECT_EQ(inst.imm, 0u, "");
    EXPECT_EQ(inst.reg, &guest_gpr.r10, "");

    // mov %ebx, 0x10(%rax)
    uint8_t mov_disp_1[] = { 0x89, 0b01011000, 0x10 };
    EXPECT_EQ(decode_instruction(mov_disp_1, 3, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_EQ(inst.mem, 4u, "");
    EXPECT_EQ(inst.imm, 0u, "");
    EXPECT_EQ(inst.reg, &guest_gpr.rbx, "");

    // mov %ebx, 0x1000000(%rax)
    uint8_t mov_disp_4[] = { 0x89, 0b10011000, 0, 0, 0, 0x1 };
    EXPECT_EQ(decode_instruction(mov_disp_4, 6, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_EQ(inst.mem, 4u, "");
    EXPECT_EQ(inst.imm, 0u, "");
    EXPECT_EQ(inst.reg, &guest_gpr.rbx, "");

    // mov %r12, 0x11(%rax)
    uint8_t rex_mov_disp[] = { 0b01001100, 0x89, 0b01100000, 0x11 };
    EXPECT_EQ(decode_instruction(rex_mov_disp, 4, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_EQ(inst.mem, 8u, "");
    EXPECT_EQ(inst.imm, 0u, "");
    EXPECT_EQ(inst.reg, &guest_gpr.r12, "");

    END_TEST;
}

static bool decode_mov_8b(void) {
    BEGIN_TEST;

    uint8_t bad_len[] = { 0x8b, 0, 0 };
    EXPECT_EQ(decode_instruction(bad_len, 3, NULL, NULL), ERR_OUT_OF_RANGE, "");
    uint8_t bad_disp[] = { 0x8b, 0b01000000 };
    EXPECT_EQ(decode_instruction(bad_disp, 2, NULL, NULL), ERR_OUT_OF_RANGE, "");
    uint8_t has_sib[] = { 0x8b, 0b01000100, 0, 0 };
    EXPECT_EQ(decode_instruction(has_sib, 4, NULL, NULL), ERR_NOT_SUPPORTED, "");

    // mov (%rax), %ecx
    uint8_t mov[] = { 0x8b, 0b00001000 };
    mx_guest_gpr_t guest_gpr;
    instruction_t inst;
    EXPECT_EQ(decode_instruction(mov, 2, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_TRUE(inst.read, "");
    EXPECT_EQ(inst.mem, 4u, "");
    EXPECT_EQ(inst.imm, 0u, "");
    EXPECT_EQ(inst.reg, &guest_gpr.rcx, "");

    // mov (%rax), %r10d
    uint8_t rex_mov[] = { 0b01000100, 0x8b, 0b00010000 };
    EXPECT_EQ(decode_instruction(rex_mov, 3, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_TRUE(inst.read, "");
    EXPECT_EQ(inst.mem, 4u, "");
    EXPECT_EQ(inst.imm, 0u, "");
    EXPECT_EQ(inst.reg, &guest_gpr.r10, "");

    // mov 0x10(%rax), %ebx
    uint8_t mov_disp_1[] = { 0x8b, 0b01011000, 0x10 };
    EXPECT_EQ(decode_instruction(mov_disp_1, 3, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_TRUE(inst.read, "");
    EXPECT_EQ(inst.mem, 4u, "");
    EXPECT_EQ(inst.imm, 0u, "");
    EXPECT_EQ(inst.reg, &guest_gpr.rbx, "");

    // mov 0x10000000(%rax), %ebx
    uint8_t mov_disp_4[] = { 0x8b, 0b10011000, 0, 0, 0, 0x1 };
    EXPECT_EQ(decode_instruction(mov_disp_4, 6, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_TRUE(inst.read, "");
    EXPECT_EQ(inst.mem, 4u, "");
    EXPECT_EQ(inst.imm, 0u, "");
    EXPECT_EQ(inst.reg, &guest_gpr.rbx, "");

    // mov 0x11(rax), %r12
    uint8_t rex_mov_disp[] = { 0b01001100, 0x8b, 0b01100000, 0x11 };
    EXPECT_EQ(decode_instruction(rex_mov_disp, 4, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_TRUE(inst.read, "");
    EXPECT_EQ(inst.mem, 8u, "");
    EXPECT_EQ(inst.imm, 0u, "");
    EXPECT_EQ(inst.reg, &guest_gpr.r12, "");

    END_TEST;
}

static bool decode_mov_c7(void) {
    BEGIN_TEST;

    uint8_t bad_len[] = { 0xc7, 0 };
    EXPECT_EQ(decode_instruction(bad_len, 2, NULL, NULL), ERR_OUT_OF_RANGE, "");
    uint8_t bad_disp[] = { 0xc7, 0b01000000 };
    EXPECT_EQ(decode_instruction(bad_disp, 2, NULL, NULL), ERR_OUT_OF_RANGE, "");
    uint8_t has_sib[] = { 0xc7, 0b01000100, 0, 0, 0, 0, 0, 0 };
    EXPECT_EQ(decode_instruction(has_sib, 8, NULL, NULL), ERR_NOT_SUPPORTED, "");
    uint8_t bad_mod_rm[] = { 0xc7, 0b00111000, 0x1, 0, 0, 0 };
    EXPECT_EQ(decode_instruction(bad_mod_rm, 6, NULL, NULL), ERR_INVALID_ARGS, "");

    // movl 0x1, (%rax)
    uint8_t mov[] = { 0xc7, 0, 0x1, 0, 0, 0 };
    mx_guest_gpr_t guest_gpr;
    instruction_t inst;
    EXPECT_EQ(decode_instruction(mov, 6, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_EQ(inst.mem, 4u, "");
    EXPECT_EQ(inst.imm, 0x1u, "");
    EXPECT_EQ(inst.reg, NULL, "");

    // movq 0x1000000, (%rax)
    uint8_t rex_mov[] = { 0b01001000, 0xc7, 0, 0, 0, 0, 0x1 };
    EXPECT_EQ(decode_instruction(rex_mov, 7, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_EQ(inst.mem, 8u, "");
    EXPECT_EQ(inst.imm, 0x1000000u, "");
    EXPECT_EQ(inst.reg, NULL, "");

    // movl 0x10, -0x1(%rbx)
    uint8_t mov_disp_1[] = { 0xc7, 0b01000011, 0xff, 0x10, 0, 0, 0 };
    EXPECT_EQ(decode_instruction(mov_disp_1, 7, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_EQ(inst.mem, 4u, "");
    EXPECT_EQ(inst.imm, 0x10u, "");
    EXPECT_EQ(inst.reg, NULL, "");

    // movl 0x1000000, -0x1000000(%rbx)
    uint8_t mov_disp_4[] = { 0xc7, 0b10000011, 0, 0, 0, 0xff, 0, 0, 0, 0x1 };
    EXPECT_EQ(decode_instruction(mov_disp_4, 10, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_EQ(inst.mem, 4u, "");
    EXPECT_EQ(inst.imm, 0x1000000u, "");
    EXPECT_EQ(inst.reg, NULL, "");

    // movq 0x1000000, -0x1(%rax)
    uint8_t rex_mov_disp[] = { 0b01001100, 0xc7, 0b01000000, 0xff, 0, 0, 0, 0x1 };
    EXPECT_EQ(decode_instruction(rex_mov_disp, 8, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_EQ(inst.mem, 8u, "");
    EXPECT_EQ(inst.imm, 0x1000000u, "");
    EXPECT_EQ(inst.reg, NULL, "");

    END_TEST;
}

static bool decode_movz_0f_b6(void) {
    BEGIN_TEST;

    uint8_t bad_len[] = { 0x0f, 0xb6, 0, 0 };
    EXPECT_EQ(decode_instruction(bad_len, 4, NULL, NULL), ERR_OUT_OF_RANGE, "");
    uint8_t bad_disp[] = { 0x0f, 0xb6, 0b01000000 };
    EXPECT_EQ(decode_instruction(bad_disp, 3, NULL, NULL), ERR_OUT_OF_RANGE, "");
    uint8_t has_sib[] = { 0x0f, 0xb6, 0b01000100, 0, 0 };
    EXPECT_EQ(decode_instruction(has_sib, 5, NULL, NULL), ERR_NOT_SUPPORTED, "");

    // movz (%rax), %ecx
    uint8_t movz[] = { 0x0f, 0xb6, 0b00001000 };
    mx_guest_gpr_t guest_gpr;
    instruction_t inst;
    EXPECT_EQ(decode_instruction(movz, 3, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_TRUE(inst.read, "");
    EXPECT_EQ(inst.mem, 1u, "");
    EXPECT_EQ(inst.imm, 0u, "");
    EXPECT_EQ(inst.reg, &guest_gpr.rcx, "");

    // movz (%rax), %r10d
    uint8_t rex_movz[] = { 0b01000100, 0x0f, 0xb6, 0b00010000 };
    EXPECT_EQ(decode_instruction(rex_movz, 4, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_TRUE(inst.read, "");
    EXPECT_EQ(inst.mem, 1u, "");
    EXPECT_EQ(inst.imm, 0u, "");
    EXPECT_EQ(inst.reg, &guest_gpr.r10, "");

    // movz 0x10(%rax), %ebx
    uint8_t movz_disp_1[] = { 0x0f, 0xb6, 0b01011000, 0x10 };
    EXPECT_EQ(decode_instruction(movz_disp_1, 4, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_TRUE(inst.read, "");
    EXPECT_EQ(inst.mem, 1u, "");
    EXPECT_EQ(inst.imm, 0u, "");
    EXPECT_EQ(inst.reg, &guest_gpr.rbx, "");

    // movz 0x10000000(%rax), %ebx
    uint8_t movz_disp_4[] = { 0x0f, 0xb6, 0b10011000, 0, 0, 0, 0x1 };
    EXPECT_EQ(decode_instruction(movz_disp_4, 7, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_TRUE(inst.read, "");
    EXPECT_EQ(inst.mem, 1u, "");
    EXPECT_EQ(inst.imm, 0u, "");
    EXPECT_EQ(inst.reg, &guest_gpr.rbx, "");

    // movz 0x11(rax), %r12
    uint8_t rex_movz_disp[] = { 0b01001100, 0x0f, 0xb6, 0b01100000, 0x11 };
    EXPECT_EQ(decode_instruction(rex_movz_disp, 5, &guest_gpr, &inst), NO_ERROR, "");
    EXPECT_TRUE(inst.read, "");
    EXPECT_EQ(inst.mem, 1u, "");
    EXPECT_EQ(inst.imm, 0u, "");
    EXPECT_EQ(inst.reg, &guest_gpr.r12, "");

    END_TEST;
}

BEGIN_TEST_CASE(decode)
RUN_TEST(decode_failure)
RUN_TEST(decode_mov_89)
RUN_TEST(decode_mov_8b)
RUN_TEST(decode_mov_c7)
RUN_TEST(decode_movz_0f_b6)
END_TEST_CASE(decode)
