// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <unittest.h>

#include <arch/x86/hypervisor_state.h>
#include <magenta/errors.h>

#include "vmexit_priv.h"

static bool decode_failure(void* context) {
    BEGIN_TEST;

    EXPECT_EQ(decode_instruction(nullptr, 0, nullptr, nullptr), ERR_BAD_STATE, "");
    EXPECT_EQ(decode_instruction(nullptr, 32, nullptr, nullptr), ERR_OUT_OF_RANGE, "");

    uint8_t bad_rex[] = { 0b0100u << 4, 0, 0 };
    EXPECT_EQ(decode_instruction(bad_rex, 1, nullptr, nullptr), ERR_NOT_SUPPORTED, "");
    EXPECT_EQ(decode_instruction(bad_rex, 2, nullptr, nullptr), ERR_OUT_OF_RANGE, "");
    EXPECT_EQ(decode_instruction(bad_rex, 3, nullptr, nullptr), ERR_NOT_SUPPORTED, "");

    uint8_t bad_len[] = { 0, 0 };
    EXPECT_EQ(decode_instruction(bad_len, 2, nullptr, nullptr), ERR_NOT_SUPPORTED, "");

    END_TEST;
}

static bool decode_mov_89(void* context) {
    BEGIN_TEST;

    uint8_t bad_len[] = { 0x89, 0, 0 };
    EXPECT_EQ(decode_instruction(bad_len, 3, nullptr, nullptr), ERR_OUT_OF_RANGE, "");
    uint8_t bad_disp[] = { 0x89, 0b01000000 };
    EXPECT_EQ(decode_instruction(bad_disp, 2, nullptr, nullptr), ERR_OUT_OF_RANGE, "");
    uint8_t has_sib[] = { 0x89, 0b01000100, 0, 0 };
    EXPECT_EQ(decode_instruction(has_sib, 4, nullptr, nullptr), ERR_NOT_SUPPORTED, "");

    // mov %ecx, (%rax)
    uint8_t mov[] = { 0x89, 0b00001000 };
    GuestState guest_state;
    Instruction inst;
    EXPECT_EQ(decode_instruction(mov, 2, &guest_state, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_FALSE(inst.rex, "");
    EXPECT_EQ(inst.val, 0u, "");
    EXPECT_EQ(inst.reg, &guest_state.rcx, "");

    // mov %r10d, (%rax)
    uint8_t rex_mov[] = { 0b01000100, 0x89, 0b00010000 };
    EXPECT_EQ(decode_instruction(rex_mov, 3, &guest_state, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_FALSE(inst.rex, "");
    EXPECT_EQ(inst.val, 0u, "");
    EXPECT_EQ(inst.reg, &guest_state.r10, "");

    // mov %ebx, 0x10(%rax)
    uint8_t mov_disp_1[] = { 0x89, 0b01011000, 0x10 };
    EXPECT_EQ(decode_instruction(mov_disp_1, 3, &guest_state, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_FALSE(inst.rex, "");
    EXPECT_EQ(inst.val, 0u, "");
    EXPECT_EQ(inst.reg, &guest_state.rbx, "");

    // mov %ebx, 0x1000000(%rax)
    uint8_t mov_disp_4[] = { 0x89, 0b10011000, 0, 0, 0, 0x1 };
    EXPECT_EQ(decode_instruction(mov_disp_4, 6, &guest_state, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_FALSE(inst.rex, "");
    EXPECT_EQ(inst.val, 0u, "");
    EXPECT_EQ(inst.reg, &guest_state.rbx, "");

    // mov %r12, 0x11(%rax)
    uint8_t rex_mov_disp[] = { 0b01001100, 0x89, 0b01100000, 0x11 };
    EXPECT_EQ(decode_instruction(rex_mov_disp, 4, &guest_state, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_TRUE(inst.rex, "");
    EXPECT_EQ(inst.val, 0u, "");
    EXPECT_EQ(inst.reg, &guest_state.r12, "");

    END_TEST;
}

static bool decode_mov_8b(void* context) {
    BEGIN_TEST;

    uint8_t bad_len[] = { 0x8b, 0, 0 };
    EXPECT_EQ(decode_instruction(bad_len, 3, nullptr, nullptr), ERR_OUT_OF_RANGE, "");
    uint8_t bad_disp[] = { 0x8b, 0b01000000 };
    EXPECT_EQ(decode_instruction(bad_disp, 2, nullptr, nullptr), ERR_OUT_OF_RANGE, "");
    uint8_t has_sib[] = { 0x8b, 0b01000100, 0, 0 };
    EXPECT_EQ(decode_instruction(has_sib, 4, nullptr, nullptr), ERR_NOT_SUPPORTED, "");

    // mov (%rax), %ecx
    uint8_t mov[] = { 0x8b, 0b00001000 };
    GuestState guest_state;
    Instruction inst;
    EXPECT_EQ(decode_instruction(mov, 2, &guest_state, &inst), NO_ERROR, "");
    EXPECT_TRUE(inst.read, "");
    EXPECT_FALSE(inst.rex, "");
    EXPECT_EQ(inst.val, 0u, "");
    EXPECT_EQ(inst.reg, &guest_state.rcx, "");

    // mov (%rax), %r10d
    uint8_t rex_mov[] = { 0b01000100, 0x8b, 0b00010000 };
    EXPECT_EQ(decode_instruction(rex_mov, 3, &guest_state, &inst), NO_ERROR, "");
    EXPECT_TRUE(inst.read, "");
    EXPECT_FALSE(inst.rex, "");
    EXPECT_EQ(inst.val, 0u, "");
    EXPECT_EQ(inst.reg, &guest_state.r10, "");

    // mov 0x10(%rax), %ebx
    uint8_t mov_disp_1[] = { 0x8b, 0b01011000, 0x10 };
    EXPECT_EQ(decode_instruction(mov_disp_1, 3, &guest_state, &inst), NO_ERROR, "");
    EXPECT_TRUE(inst.read, "");
    EXPECT_FALSE(inst.rex, "");
    EXPECT_EQ(inst.val, 0u, "");
    EXPECT_EQ(inst.reg, &guest_state.rbx, "");

    // mov 0x10000000(%rax), %ebx
    uint8_t mov_disp_4[] = { 0x8b, 0b10011000, 0, 0, 0, 0x1 };
    EXPECT_EQ(decode_instruction(mov_disp_4, 6, &guest_state, &inst), NO_ERROR, "");
    EXPECT_TRUE(inst.read, "");
    EXPECT_FALSE(inst.rex, "");
    EXPECT_EQ(inst.val, 0u, "");
    EXPECT_EQ(inst.reg, &guest_state.rbx, "");

    // mov 0x11(rax), %r12
    uint8_t rex_mov_disp[] = { 0b01001100, 0x8b, 0b01100000, 0x11 };
    EXPECT_EQ(decode_instruction(rex_mov_disp, 4, &guest_state, &inst), NO_ERROR, "");
    EXPECT_TRUE(inst.read, "");
    EXPECT_TRUE(inst.rex, "");
    EXPECT_EQ(inst.val, 0u, "");
    EXPECT_EQ(inst.reg, &guest_state.r12, "");

    END_TEST;
}

static bool decode_mov_c7(void* context) {
    BEGIN_TEST;

    uint8_t bad_len[] = { 0xc7, 0 };
    EXPECT_EQ(decode_instruction(bad_len, 2, nullptr, nullptr), ERR_OUT_OF_RANGE, "");
    uint8_t bad_disp[] = { 0xc7, 0b01000000 };
    EXPECT_EQ(decode_instruction(bad_disp, 2, nullptr, nullptr), ERR_OUT_OF_RANGE, "");
    uint8_t has_sib[] = { 0xc7, 0b01000100, 0, 0, 0, 0, 0, 0 };
    EXPECT_EQ(decode_instruction(has_sib, 8, nullptr, nullptr), ERR_NOT_SUPPORTED, "");
    uint8_t bad_mod_rm[] = { 0xc7, 0b00111000, 0x1, 0, 0, 0 };
    EXPECT_EQ(decode_instruction(bad_mod_rm, 6, nullptr, nullptr), ERR_INVALID_ARGS, "");

    // movl 0x1, (%rax)
    uint8_t mov[] = { 0xc7, 0, 0x1, 0, 0, 0 };
    GuestState guest_state;
    Instruction inst;
    EXPECT_EQ(decode_instruction(mov, 6, &guest_state, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_FALSE(inst.rex, "");
    EXPECT_EQ(inst.val, 0x1u, "");
    EXPECT_EQ(inst.reg, nullptr, "");

    // movq 0x1000000, (%rax)
    uint8_t rex_mov[] = { 0b01001000, 0xc7, 0, 0, 0, 0, 0x1 };
    EXPECT_EQ(decode_instruction(rex_mov, 7, &guest_state, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_FALSE(inst.rex, "");
    EXPECT_EQ(inst.val, 0x1000000u, "");
    EXPECT_EQ(inst.reg, nullptr, "");

    // movl 0x10, -0x1(%rbx)
    uint8_t mov_disp_1[] = { 0xc7, 0b01000011, 0xff, 0x10, 0, 0, 0 };
    EXPECT_EQ(decode_instruction(mov_disp_1, 7, &guest_state, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_FALSE(inst.rex, "");
    EXPECT_EQ(inst.val, 0x10u, "");
    EXPECT_EQ(inst.reg, nullptr, "");

    // movl 0x1000000, -0x1000000(%rbx)
    uint8_t mov_disp_4[] = { 0xc7, 0b10000011, 0, 0, 0, 0xff, 0, 0, 0, 0x1 };
    EXPECT_EQ(decode_instruction(mov_disp_4, 10, &guest_state, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_FALSE(inst.rex, "");
    EXPECT_EQ(inst.val, 0x1000000u, "");
    EXPECT_EQ(inst.reg, nullptr, "");

    // movq 0x1000000, -0x1(%rax)
    uint8_t rex_mov_disp[] = { 0b01001100, 0xc7, 0b01000000, 0xff, 0, 0, 0, 0x1 };
    EXPECT_EQ(decode_instruction(rex_mov_disp, 8, &guest_state, &inst), NO_ERROR, "");
    EXPECT_FALSE(inst.read, "");
    EXPECT_FALSE(inst.rex, "");
    EXPECT_EQ(inst.val, 0x1000000u, "");
    EXPECT_EQ(inst.reg, nullptr, "");

    END_TEST;
}

UNITTEST_START_TESTCASE(vmexit_tests)
UNITTEST("decode_failure", decode_failure)
UNITTEST("decode_mov_89", decode_mov_89)
UNITTEST("decode_mov_8b", decode_mov_8b)
UNITTEST("decode_mov_c7", decode_mov_c7)
UNITTEST_END_TESTCASE(vmexit_tests, "vmexit", "VM exit tests", nullptr, nullptr);
