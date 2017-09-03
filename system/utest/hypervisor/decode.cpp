// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/decode.h>
#include <magenta/errors.h>
#include <magenta/syscalls/hypervisor.h>
#include <unittest/unittest.h>

static bool decode_failure(void) {
    BEGIN_TEST;

    EXPECT_EQ(inst_decode(nullptr, 0, nullptr, nullptr), MX_ERR_BAD_STATE);
    EXPECT_EQ(inst_decode(nullptr, 32, nullptr, nullptr), MX_ERR_OUT_OF_RANGE);

    uint8_t bad_rex[] = {0b0100u << 4, 0, 0};
    EXPECT_EQ(inst_decode(bad_rex, 1, nullptr, nullptr), MX_ERR_NOT_SUPPORTED);
    EXPECT_EQ(inst_decode(bad_rex, 2, nullptr, nullptr), MX_ERR_OUT_OF_RANGE);
    EXPECT_EQ(inst_decode(bad_rex, 3, nullptr, nullptr), MX_ERR_NOT_SUPPORTED);

    uint8_t bad_len[] = {0, 0};
    EXPECT_EQ(inst_decode(bad_len, 2, nullptr, nullptr), MX_ERR_NOT_SUPPORTED);

    END_TEST;
}

static bool decode_mov_89(void) {
    BEGIN_TEST;

    uint8_t bad_len[] = {0x89, 0, 0};
    EXPECT_EQ(inst_decode(bad_len, 3, nullptr, nullptr), MX_ERR_OUT_OF_RANGE);
    uint8_t bad_disp[] = {0x89, 0b01000000};
    EXPECT_EQ(inst_decode(bad_disp, 2, nullptr, nullptr), MX_ERR_OUT_OF_RANGE);
    uint8_t has_sib[] = {0x89, 0b01000100, 0, 0};
    EXPECT_EQ(inst_decode(has_sib, 4, nullptr, nullptr), MX_ERR_NOT_SUPPORTED);
    uint8_t bad_h66[] = {0x66, 0b01001000, 0x89, 0b00010000};
    EXPECT_EQ(inst_decode(bad_h66, 4, nullptr, nullptr), MX_ERR_NOT_SUPPORTED);

    // mov %ecx, (%rax)
    uint8_t mov[] = {0x89, 0b00001000};
    mx_vcpu_state_t vcpu_state;
    instruction_t inst;
    EXPECT_EQ(inst_decode(mov, 2, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_WRITE);
    EXPECT_EQ(inst.mem, 4u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.rcx);
    EXPECT_NULL(inst.flags);

    // mov %r10d, (%rax)
    uint8_t rex_mov[] = {0b01000100, 0x89, 0b00010000};
    EXPECT_EQ(inst_decode(rex_mov, 3, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_WRITE);
    EXPECT_EQ(inst.mem, 4u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.r10);
    EXPECT_NULL(inst.flags);

    // mov %ebx, 0x10(%rax)
    uint8_t mov_disp_1[] = {0x89, 0b01011000, 0x10};
    EXPECT_EQ(inst_decode(mov_disp_1, 3, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_WRITE);
    EXPECT_EQ(inst.mem, 4u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.rbx);
    EXPECT_NULL(inst.flags);

    // mov %ebx, 0x1000000(%rax)
    uint8_t mov_disp_4[] = {0x89, 0b10011000, 0, 0, 0, 0x1};
    EXPECT_EQ(inst_decode(mov_disp_4, 6, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_WRITE);
    EXPECT_EQ(inst.mem, 4u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.rbx);
    EXPECT_NULL(inst.flags);

    // mov %r12, 0x11(%rax)
    uint8_t rex_mov_disp[] = {0b01001100, 0x89, 0b01100000, 0x11};
    EXPECT_EQ(inst_decode(rex_mov_disp, 4, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_WRITE);
    EXPECT_EQ(inst.mem, 8u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.r12);
    EXPECT_NULL(inst.flags);

    // mov %r14w, 0x13(%rax)
    uint8_t h66_mov_disp[] = {0x66, 0b01000100, 0x89, 0b01110000, 0x13};
    EXPECT_EQ(inst_decode(h66_mov_disp, 5, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_WRITE);
    EXPECT_EQ(inst.mem, 2u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.r14);
    EXPECT_NULL(inst.flags);

    END_TEST;
}

static bool decode_mov_8b(void) {
    BEGIN_TEST;

    uint8_t bad_len[] = {0x8b, 0, 0};
    EXPECT_EQ(inst_decode(bad_len, 3, nullptr, nullptr), MX_ERR_OUT_OF_RANGE);
    uint8_t bad_disp[] = {0x8b, 0b01000000};
    EXPECT_EQ(inst_decode(bad_disp, 2, nullptr, nullptr), MX_ERR_OUT_OF_RANGE);
    uint8_t has_sib[] = {0x8b, 0b01000100, 0, 0};
    EXPECT_EQ(inst_decode(has_sib, 4, nullptr, nullptr), MX_ERR_NOT_SUPPORTED);
    uint8_t bad_h66[] = {0x66, 0b01001000, 0x8b, 0b00010000};
    EXPECT_EQ(inst_decode(bad_h66, 4, nullptr, nullptr), MX_ERR_NOT_SUPPORTED);

    // mov (%rax), %ecx
    uint8_t mov[] = {0x8b, 0b00001000};
    mx_vcpu_state_t vcpu_state;
    instruction_t inst;
    EXPECT_EQ(inst_decode(mov, 2, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_READ);
    EXPECT_EQ(inst.mem, 4u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.rcx);
    EXPECT_NULL(inst.flags);

    // mov (%rax), %r10d
    uint8_t rex_mov[] = {0b01000100, 0x8b, 0b00010000};
    EXPECT_EQ(inst_decode(rex_mov, 3, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_READ);
    EXPECT_EQ(inst.mem, 4u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.r10);
    EXPECT_NULL(inst.flags);

    // mov 0x10(%rax), %ebx
    uint8_t mov_disp_1[] = {0x8b, 0b01011000, 0x10};
    EXPECT_EQ(inst_decode(mov_disp_1, 3, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_READ);
    EXPECT_EQ(inst.mem, 4u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.rbx);
    EXPECT_NULL(inst.flags);

    // mov 0x10000000(%rax), %ebx
    uint8_t mov_disp_4[] = {0x8b, 0b10011000, 0, 0, 0, 0x1};
    EXPECT_EQ(inst_decode(mov_disp_4, 6, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_READ);
    EXPECT_EQ(inst.mem, 4u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.rbx);
    EXPECT_NULL(inst.flags);

    // mov 0x11(rax), %r12
    uint8_t rex_mov_disp[] = {0b01001100, 0x8b, 0b01100000, 0x11};
    EXPECT_EQ(inst_decode(rex_mov_disp, 4, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_READ);
    EXPECT_EQ(inst.mem, 8u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.r12);
    EXPECT_NULL(inst.flags);

    // mov 0x13(rax), %r14w
    uint8_t h66_mov_disp[] = {0x66, 0b01000100, 0x8b, 0b01110000, 0x13};
    EXPECT_EQ(inst_decode(h66_mov_disp, 5, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_READ);
    EXPECT_EQ(inst.mem, 2u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.r14);
    EXPECT_NULL(inst.flags);

    END_TEST;
}

static bool decode_mov_c7(void) {
    BEGIN_TEST;

    uint8_t bad_len[] = {0xc7, 0};
    EXPECT_EQ(inst_decode(bad_len, 2, nullptr, nullptr), MX_ERR_OUT_OF_RANGE);
    uint8_t bad_disp[] = {0xc7, 0b01000000};
    EXPECT_EQ(inst_decode(bad_disp, 2, nullptr, nullptr), MX_ERR_OUT_OF_RANGE);
    uint8_t has_sib[] = {0xc7, 0b01000100, 0, 0, 0, 0, 0, 0};
    EXPECT_EQ(inst_decode(has_sib, 8, nullptr, nullptr), MX_ERR_NOT_SUPPORTED);
    uint8_t bad_mod_rm[] = {0xc7, 0b00111000, 0x1, 0, 0, 0};
    EXPECT_EQ(inst_decode(bad_mod_rm, 6, nullptr, nullptr), MX_ERR_INVALID_ARGS);
    uint8_t bad_h66[] = {0x66, 0b01001000, 0xc7, 0, 0, 0, 0, 0x1};
    EXPECT_EQ(inst_decode(bad_h66, 8, nullptr, nullptr), MX_ERR_NOT_SUPPORTED);

    // movl 0x1, (%rax)
    uint8_t mov[] = {0xc7, 0, 0x1, 0, 0, 0};
    mx_vcpu_state_t vcpu_state;
    instruction_t inst;
    EXPECT_EQ(inst_decode(mov, 6, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_WRITE);
    EXPECT_EQ(inst.mem, 4u);
    EXPECT_EQ(inst.imm, 0x1u);
    EXPECT_NULL(inst.reg);
    EXPECT_NULL(inst.flags);

    // movq 0x1000000, (%rax)
    uint8_t rex_mov[] = {0b01001000, 0xc7, 0, 0, 0, 0, 0x1};
    EXPECT_EQ(inst_decode(rex_mov, 7, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_WRITE);
    EXPECT_EQ(inst.mem, 8u);
    EXPECT_EQ(inst.imm, 0x1000000u);
    EXPECT_NULL(inst.reg);
    EXPECT_NULL(inst.flags);

    // movl 0x10, -0x1(%rbx)
    uint8_t mov_disp_1[] = {0xc7, 0b01000011, 0xff, 0x10, 0, 0, 0};
    EXPECT_EQ(inst_decode(mov_disp_1, 7, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_WRITE);
    EXPECT_EQ(inst.mem, 4u);
    EXPECT_EQ(inst.imm, 0x10u);
    EXPECT_NULL(inst.reg);
    EXPECT_NULL(inst.flags);

    // movl 0x1000000, -0x1000000(%rbx)
    uint8_t mov_disp_4[] = {0xc7, 0b10000011, 0, 0, 0, 0xff, 0, 0, 0, 0x1};
    EXPECT_EQ(inst_decode(mov_disp_4, 10, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_WRITE);
    EXPECT_EQ(inst.mem, 4u);
    EXPECT_EQ(inst.imm, 0x1000000u);
    EXPECT_NULL(inst.reg);
    EXPECT_NULL(inst.flags);

    // movw 0x100, -0x1(%rax)
    uint8_t h66_mov_disp[] = {0x66, 0b01000100, 0xc7, 0b01000000, 0xff, 0, 0x1};
    EXPECT_EQ(inst_decode(h66_mov_disp, 7, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_WRITE);
    EXPECT_EQ(inst.mem, 2u);
    EXPECT_EQ(inst.imm, 0x100u);
    EXPECT_NULL(inst.reg);
    EXPECT_NULL(inst.flags);

    END_TEST;
}

static bool decode_movz_0f_b6(void) {
    BEGIN_TEST;

    uint8_t bad_len[] = {0x0f, 0xb6, 0, 0};
    EXPECT_EQ(inst_decode(bad_len, 4, nullptr, nullptr), MX_ERR_OUT_OF_RANGE);
    uint8_t bad_disp[] = {0x0f, 0xb6, 0b01000000};
    EXPECT_EQ(inst_decode(bad_disp, 3, nullptr, nullptr), MX_ERR_OUT_OF_RANGE);
    uint8_t has_sib[] = {0x0f, 0xb6, 0b01000100, 0, 0};
    EXPECT_EQ(inst_decode(has_sib, 5, nullptr, nullptr), MX_ERR_NOT_SUPPORTED);
    uint8_t has_h66[] = {0x66, 0x0f, 0xb6, 0b00001000};
    EXPECT_EQ(inst_decode(has_h66, 4, nullptr, nullptr), MX_ERR_BAD_STATE);

    // movzb (%rax), %ecx
    uint8_t movz[] = {0x0f, 0xb6, 0b00001000};
    mx_vcpu_state_t vcpu_state;
    instruction_t inst;
    EXPECT_EQ(inst_decode(movz, 3, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_READ);
    EXPECT_EQ(inst.mem, 1u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.rcx);
    EXPECT_NULL(inst.flags);

    // movzb (%rax), %r10d
    uint8_t rex_movz[] = {0b01000100, 0x0f, 0xb6, 0b00010000};
    EXPECT_EQ(inst_decode(rex_movz, 4, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_READ);
    EXPECT_EQ(inst.mem, 1u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.r10);
    EXPECT_NULL(inst.flags);

    // movzb 0x10(%rax), %ebx
    uint8_t movz_disp_1[] = {0x0f, 0xb6, 0b01011000, 0x10};
    EXPECT_EQ(inst_decode(movz_disp_1, 4, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_READ);
    EXPECT_EQ(inst.mem, 1u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.rbx);
    EXPECT_NULL(inst.flags);

    // movzb 0x10000000(%rax), %ebx
    uint8_t movz_disp_4[] = {0x0f, 0xb6, 0b10011000, 0, 0, 0, 0x1};
    EXPECT_EQ(inst_decode(movz_disp_4, 7, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_READ);
    EXPECT_EQ(inst.mem, 1u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.rbx);
    EXPECT_NULL(inst.flags);

    // movzb 0x11(rax), %r12
    uint8_t rex_movz_disp[] = {0b01001100, 0x0f, 0xb6, 0b01100000, 0x11};
    EXPECT_EQ(inst_decode(rex_movz_disp, 5, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_READ);
    EXPECT_EQ(inst.mem, 1u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.r12);
    EXPECT_NULL(inst.flags);

    END_TEST;
}

static bool decode_movz_0f_b7(void) {
    BEGIN_TEST;

    uint8_t bad_len[] = {0x0f, 0xb7, 0, 0};
    EXPECT_EQ(inst_decode(bad_len, 4, nullptr, nullptr), MX_ERR_OUT_OF_RANGE);
    uint8_t bad_disp[] = {0x0f, 0xb7, 0b01000000};
    EXPECT_EQ(inst_decode(bad_disp, 3, nullptr, nullptr), MX_ERR_OUT_OF_RANGE);
    uint8_t has_sib[] = {0x0f, 0xb7, 0b01000100, 0, 0};
    EXPECT_EQ(inst_decode(has_sib, 5, nullptr, nullptr), MX_ERR_NOT_SUPPORTED);
    uint8_t has_h66[] = {0x66, 0x0f, 0xb7, 0b00001000};
    EXPECT_EQ(inst_decode(has_h66, 4, nullptr, nullptr), MX_ERR_BAD_STATE);

    // movzw (%rax), %ecx
    uint8_t movz[] = {0x0f, 0xb7, 0b00001000};
    mx_vcpu_state_t vcpu_state;
    instruction_t inst;
    EXPECT_EQ(inst_decode(movz, 3, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_READ);
    EXPECT_EQ(inst.mem, 2u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.rcx);
    EXPECT_NULL(inst.flags);

    // movzw (%rax), %r10d
    uint8_t rex_movz[] = {0b01000100, 0x0f, 0xb7, 0b00010000};
    EXPECT_EQ(inst_decode(rex_movz, 4, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_READ);
    EXPECT_EQ(inst.mem, 2u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.r10);
    EXPECT_NULL(inst.flags);

    // movzw 0x10(%rax), %ebx
    uint8_t movz_disp_1[] = {0x0f, 0xb7, 0b01011000, 0x10};
    EXPECT_EQ(inst_decode(movz_disp_1, 4, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_READ);
    EXPECT_EQ(inst.mem, 2u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.rbx);
    EXPECT_NULL(inst.flags);

    // movzw 0x10000000(%rax), %ebx
    uint8_t movz_disp_4[] = {0x0f, 0xb7, 0b10011000, 0, 0, 0, 0x1};
    EXPECT_EQ(inst_decode(movz_disp_4, 7, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_READ);
    EXPECT_EQ(inst.mem, 2u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.rbx);
    EXPECT_NULL(inst.flags);

    // movzw 0x11(rax), %r12
    uint8_t rex_movz_disp[] = {0b01001100, 0x0f, 0xb7, 0b01100000, 0x11};
    EXPECT_EQ(inst_decode(rex_movz_disp, 5, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_MOV_READ);
    EXPECT_EQ(inst.mem, 2u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.r12);
    EXPECT_NULL(inst.flags);

    END_TEST;
}

static bool decode_test_f6(void) {
    BEGIN_TEST;

    uint8_t bad_len[] = {0xf6, 0};
    EXPECT_EQ(inst_decode(bad_len, 2, nullptr, nullptr), MX_ERR_OUT_OF_RANGE);
    uint8_t bad_disp[] = {0xf6, 0b01000000, 0};
    EXPECT_EQ(inst_decode(bad_disp, 3, nullptr, nullptr), MX_ERR_OUT_OF_RANGE);
    uint8_t has_sib[] = {0xf6, 0b01000100, 0, 0};
    EXPECT_EQ(inst_decode(has_sib, 4, nullptr, nullptr), MX_ERR_NOT_SUPPORTED);
    uint8_t bad_mod_rm[] = {0xf6, 0b00111000, 0x1};
    EXPECT_EQ(inst_decode(bad_mod_rm, 3, nullptr, nullptr), MX_ERR_INVALID_ARGS);
    uint8_t has_h66[] = {0x66, 0xf6, 0b00001000, 0};
    EXPECT_EQ(inst_decode(has_h66, 4, nullptr, nullptr), MX_ERR_BAD_STATE);

    // test 0x1, (%rax)
    uint8_t test[] = {0xf6, 0, 0x1};
    mx_vcpu_state_t vcpu_state;
    instruction_t inst;
    EXPECT_EQ(inst_decode(test, 3, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_TEST);
    EXPECT_EQ(inst.mem, 1u);
    EXPECT_EQ(inst.imm, 0x1u);
    EXPECT_NULL(inst.reg);
    EXPECT_EQ(inst.flags, &vcpu_state.flags);

    // test 0x10, -0x1(%rbx)
    uint8_t test_disp_1[] = {0xf6, 0b01000011, 0xff, 0x10};
    EXPECT_EQ(inst_decode(test_disp_1, 4, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_TEST);
    EXPECT_EQ(inst.mem, 1u);
    EXPECT_EQ(inst.imm, 0x10u);
    EXPECT_NULL(inst.reg);
    EXPECT_EQ(inst.flags, &vcpu_state.flags);

    // test 0x11, -0x1000000(%rbx)
    uint8_t test_disp_4[] = {0xf6, 0b10000011, 0, 0, 0, 0xff, 0x11};
    EXPECT_EQ(inst_decode(test_disp_4, 7, &vcpu_state, &inst), MX_OK);
    EXPECT_EQ(inst.type, INST_TEST);
    EXPECT_EQ(inst.mem, 1u);
    EXPECT_EQ(inst.imm, 0x11u);
    EXPECT_NULL(inst.reg);
    EXPECT_EQ(inst.flags, &vcpu_state.flags);

    END_TEST;
}

static bool test_computing_flags(void) {
    BEGIN_TEST;

    EXPECT_EQ(x86_flags_for_test8(1, 1), 2);
    EXPECT_EQ(x86_flags_for_test8(1, -1), 2);
    EXPECT_EQ(x86_flags_for_test8(-1, 1), 2);
    EXPECT_EQ(x86_flags_for_test8(3, 3), 6);
    EXPECT_EQ(x86_flags_for_test8(0, 0), 0x46);
    EXPECT_EQ(x86_flags_for_test8(-1, -1), 0x86);

    END_TEST;
}

BEGIN_TEST_CASE(decode)
RUN_TEST(decode_failure)
RUN_TEST(decode_mov_89)
RUN_TEST(decode_mov_8b)
RUN_TEST(decode_mov_c7)
RUN_TEST(decode_movz_0f_b6)
RUN_TEST(decode_movz_0f_b7)
RUN_TEST(decode_test_f6)
RUN_TEST(test_computing_flags)
END_TEST_CASE(decode)
