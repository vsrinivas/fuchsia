// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/errors.h>
#include <zircon/syscalls/hypervisor.h>

#include "garnet/lib/machina/arch/x86/decode.h"
#include "gtest/gtest.h"

namespace machina {
namespace {

TEST(InstDecode, failure) {
  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(inst_decode(nullptr, 0, 4, &vcpu_state, nullptr),
            ZX_ERR_BAD_STATE);
  ASSERT_EQ(inst_decode(nullptr, 32, 4, &vcpu_state, nullptr),
            ZX_ERR_OUT_OF_RANGE);

  uint8_t bad_rex[] = {0b0100'0000, 0x00, 0b00'000'000};  // rex opcode modrm
  ASSERT_EQ(inst_decode(bad_rex, 1, 4, &vcpu_state, nullptr),
            ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(inst_decode(bad_rex, 2, 4, &vcpu_state, nullptr),
            ZX_ERR_OUT_OF_RANGE);
  ASSERT_EQ(inst_decode(bad_rex, 3, 4, &vcpu_state, nullptr),
            ZX_ERR_NOT_SUPPORTED);

  uint8_t bad_len[] = {0x00, 0b00'000'000};  // opcode modrm
  ASSERT_EQ(inst_decode(bad_len, 2, 4, &vcpu_state, nullptr),
            ZX_ERR_NOT_SUPPORTED);

  ASSERT_EQ(inst_decode(bad_len, 2, 3, &vcpu_state, nullptr),
            ZX_ERR_NOT_SUPPORTED);
}

TEST(InstDecode, mov_89) {
  zx_vcpu_state_t vcpu_state;
  uint8_t bad_len[] = {0x89, 0, 0};  // opcode modrm ?
  ASSERT_EQ(inst_decode(bad_len, 3, 4, &vcpu_state, nullptr),
            ZX_ERR_OUT_OF_RANGE);
  uint8_t bad_disp[] = {0x89, 0b01'000'000};  // opcode modrm
  ASSERT_EQ(inst_decode(bad_disp, 2, 4, &vcpu_state, nullptr),
            ZX_ERR_OUT_OF_RANGE);
  uint8_t bad_h66[] = {0x66, 0b0100'1000, 0x89,
                       0b00'010'000};  // h66 rex opcode modrm
  ASSERT_EQ(inst_decode(bad_h66, 4, 4, &vcpu_state, nullptr),
            ZX_ERR_NOT_SUPPORTED);

  // mov %ecx, (%rax)
  uint8_t mov[] = {0x89, 0b00'001'000};  // opcode modrm
  Instruction inst;
  ASSERT_EQ(inst_decode(mov, 2, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rcx);
  EXPECT_EQ(inst.flags, nullptr);

  // movw %cx, (%rax)
  uint8_t mov_16bit[] = {0x89, 0b00'001'000};  // opcode modrm
  ASSERT_EQ(inst_decode(mov_16bit, 2, 2, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 2u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rcx);
  EXPECT_EQ(inst.flags, nullptr);

  // mov %r10d, (%rax)
  uint8_t rex_mov[] = {0b0100'0100, 0x89, 0b00'010'000};  // rex opcode modrm
  ASSERT_EQ(inst_decode(rex_mov, 3, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.r10);
  EXPECT_EQ(inst.flags, nullptr);

  // mov %ebx, 0x10(%rax)
  uint8_t mov_disp_1[] = {0x89, 0b01'011'000, 0x10};  // opcode modrm disp
  ASSERT_EQ(inst_decode(mov_disp_1, 3, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rbx);
  EXPECT_EQ(inst.flags, nullptr);

  // mov %ebx, 0x1000000(%rax)
  uint8_t mov_disp_4[] = {0x89, 0b10'011'000, 0, 0,
                          0,    0x1};  // opcode modrm dis4 disp3 disp2 disp1
  ASSERT_EQ(inst_decode(mov_disp_4, 6, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rbx);
  EXPECT_EQ(inst.flags, nullptr);

  // mov %r12, 0x11(%rax)
  uint8_t rex_mov_disp[] = {0b0100'1100, 0x89, 0b01'100'000,
                            0x11};  // rex opcode modrm disp
  ASSERT_EQ(inst_decode(rex_mov_disp, 4, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 8u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.r12);
  EXPECT_EQ(inst.flags, nullptr);

  // mov %r14w, 0x13(%rax)
  uint8_t h66_mov_disp[] = {0x66, 0b0100'0100, 0x89, 0b01'110'000,
                            0x13};  // h66 rex opcode modrm disp
  ASSERT_EQ(inst_decode(h66_mov_disp, 5, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 2u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.r14);
  EXPECT_EQ(inst.flags, nullptr);

  // mov %ebx, (%rax,%rcx,2)
  uint8_t mov_sib[] = {0x89, 0b00'011'100, 0b01'001'000};  // opcode modrm sib
  ASSERT_EQ(inst_decode(mov_sib, 3, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rbx);
  EXPECT_EQ(inst.flags, nullptr);

  // mov %ebx, 0x04(%rax,%rcx,1)
  uint8_t mov_sib_disp[] = {0x89, 0b01'011'100, 0b00'001'000,
                            0x04};  // opcode modrm sib disp
  ASSERT_EQ(inst_decode(mov_sib_disp, 4, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rbx);
  EXPECT_EQ(inst.flags, nullptr);

  // mov %eax, 0x00ABCDEF
  uint8_t mov_sib_nobase[] = {
      0x89, 0b00'000'100, 0b00'100'101, 0xEF,
      0xCD, 0xAB,         0x00};  // opcode modrm sib disp4 disp3 disp2 disp1
  ASSERT_EQ(inst_decode(mov_sib_nobase, 7, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rax);
  EXPECT_EQ(inst.flags, nullptr);
}

// 8-bit tests to compliment decode_mov_89.
TEST(InstDecode, mov_88) {
  zx_vcpu_state_t vcpu_state;
  Instruction inst;

  // movb %ah, (%rsi)
  uint8_t mov_ah[] = {0x88, 0b00'100'110};  // opcode modrm
  ASSERT_EQ(inst_decode(mov_ah, 2, 4, &vcpu_state, &inst),
            ZX_ERR_NOT_SUPPORTED);
  // movb %bh, (%rsi)
  uint8_t mov_bh[] = {0x88, 0b00'111'110};  // opcode modrm
  ASSERT_EQ(inst_decode(mov_bh, 2, 4, &vcpu_state, &inst),
            ZX_ERR_NOT_SUPPORTED);
  // movb %ch, (%rsi)
  uint8_t mov_ch[] = {0x88, 0b00'101'110};  // opcode modrm
  ASSERT_EQ(inst_decode(mov_ch, 2, 4, &vcpu_state, &inst),
            ZX_ERR_NOT_SUPPORTED);
  // movb %dh, (%rsi)
  uint8_t mov_dh[] = {0x88, 0b00'110'110};  // opcode modrm
  ASSERT_EQ(inst_decode(mov_dh, 2, 4, &vcpu_state, &inst),
            ZX_ERR_NOT_SUPPORTED);

  // movb %dil,(%rsi)
  uint8_t rex_mov[] = {0b0100'0000, 0x88, 0b00'111'110};  // rex opcode modrm
  ASSERT_EQ(inst_decode(rex_mov, 3, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rdi);
  EXPECT_EQ(inst.flags, nullptr);
}

TEST(InstDecode, mov_8b) {
  zx_vcpu_state_t vcpu_state;
  uint8_t bad_len[] = {0x8b, 0, 0};  // opcode modrm ?
  ASSERT_EQ(inst_decode(bad_len, 3, 4, &vcpu_state, nullptr),
            ZX_ERR_OUT_OF_RANGE);
  uint8_t bad_disp[] = {0x8b, 0b01'000'000};  // opcode modrm
  ASSERT_EQ(inst_decode(bad_disp, 2, 4, &vcpu_state, nullptr),
            ZX_ERR_OUT_OF_RANGE);
  uint8_t bad_h66[] = {0x66, 0b0100'1000, 0x8b,
                       0b00'010'000};  // h66 rex opcode modrm
  ASSERT_EQ(inst_decode(bad_h66, 4, 4, &vcpu_state, nullptr),
            ZX_ERR_NOT_SUPPORTED);

  // mov (%rax), %ecx
  uint8_t mov[] = {0x8b, 0b00'001'000};  // opcode modrm
  Instruction inst;
  ASSERT_EQ(inst_decode(mov, 2, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rcx);
  EXPECT_EQ(inst.flags, nullptr);

  // movw (%rax), %cx
  uint8_t mov_16bit[] = {0x8b, 0b00'001'000};  // opcode modrm
  ASSERT_EQ(inst_decode(mov_16bit, 2, 2, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 2u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rcx);
  EXPECT_EQ(inst.flags, nullptr);

  // mov (%rax), %r10d
  uint8_t rex_mov[] = {0b0100'0100, 0x8b, 0b00'010'000};  // rex opcode modrm
  ASSERT_EQ(inst_decode(rex_mov, 3, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.r10);
  EXPECT_EQ(inst.flags, nullptr);

  // mov 0x10(%rax), %ebx
  uint8_t mov_disp_1[] = {0x8b, 0b01'011'000, 0x10};  // opcode modrm disp
  ASSERT_EQ(inst_decode(mov_disp_1, 3, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rbx);
  EXPECT_EQ(inst.flags, nullptr);

  // mov 0x10000000(%rax), %ebx
  uint8_t mov_disp_4[] = {0x8b, 0b10'011'000, 0, 0,
                          0,    0x1};  // opcode modrm disp4 disp3 disp2 disp1
  ASSERT_EQ(inst_decode(mov_disp_4, 6, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rbx);
  EXPECT_EQ(inst.flags, nullptr);

  // mov 0x11(rax), %r12
  uint8_t rex_mov_disp[] = {0b0100'1100, 0x8b, 0b01'100'000,
                            0x11};  // rex opcode modrm disp
  ASSERT_EQ(inst_decode(rex_mov_disp, 4, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 8u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.r12);
  EXPECT_EQ(inst.flags, nullptr);

  // mov 0x13(rax), %r14w
  uint8_t h66_mov_disp[] = {0x66, 0b0100'0100, 0x8b, 0b01'110'000,
                            0x13};  // h66 rex opcode modrm disp
  ASSERT_EQ(inst_decode(h66_mov_disp, 5, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 2u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.r14);
  EXPECT_EQ(inst.flags, nullptr);

  // mov (%rax,%rcx,2), %ebx
  uint8_t mov_sib[] = {0x8b, 0b00'011'100, 0b01'001'000};  // opcode modrm sib
  ASSERT_EQ(inst_decode(mov_sib, 3, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rbx);
  EXPECT_EQ(inst.flags, nullptr);

  // mov 0x04(%rax,%rcx,1), %ebx
  uint8_t mov_sib_disp[] = {0x8b, 0b01'011'100, 0b00'001'000,
                            0x04};  // opcode modrm sib disp
  ASSERT_EQ(inst_decode(mov_sib_disp, 4, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rbx);
  EXPECT_EQ(inst.flags, nullptr);

  // mov 0xABCDEF, %eax
  uint8_t mov_sib_nobase[] = {
      0x8b, 0b00'000'100, 0b00'100'101, 0xEF,
      0xCD, 0xAB,         0x00};  // opcode modrm sib disp4 disp3 disp2 disp1
  ASSERT_EQ(inst_decode(mov_sib_nobase, 7, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rax);
  EXPECT_EQ(inst.flags, nullptr);
}

// 8-bit tests to compliment decode_mov_8b.
TEST(InstDecode, mov_8a) {
  zx_vcpu_state_t vcpu_state;
  Instruction inst;

  // movb (%rsi), %ah
  uint8_t mov_ah[] = {0x8a, 0b00'100'110};  // opcode modrm
  ASSERT_EQ(inst_decode(mov_ah, 2, 4, &vcpu_state, &inst),
            ZX_ERR_NOT_SUPPORTED);
  // movb (%rsi), %bh
  uint8_t mov_bh[] = {0x8a, 0b00'111'110};  // opcode modrm
  ASSERT_EQ(inst_decode(mov_bh, 2, 4, &vcpu_state, &inst),
            ZX_ERR_NOT_SUPPORTED);
  // movb (%rsi), %ch
  uint8_t mov_ch[] = {0x8a, 0b00'101'110};  // opcode modrm
  ASSERT_EQ(inst_decode(mov_ch, 2, 4, &vcpu_state, &inst),
            ZX_ERR_NOT_SUPPORTED);
  // movb (%rsi), %dh
  uint8_t mov_dh[] = {0x8a, 0b00'110'110};  // opcode modrm
  ASSERT_EQ(inst_decode(mov_dh, 2, 4, &vcpu_state, &inst),
            ZX_ERR_NOT_SUPPORTED);

  // movb (%rsi)
  uint8_t rex_mov[] = {0b0100'0000, 0x8a, 0b00'111'110};  // rex opcode modrm
  ASSERT_EQ(inst_decode(rex_mov, 3, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rdi);
  EXPECT_EQ(inst.flags, nullptr);
}

TEST(InstDecode, mov_c7) {
  zx_vcpu_state_t vcpu_state;
  uint8_t bad_len[] = {0xc7, 0};  // opcode modrm
  ASSERT_EQ(inst_decode(bad_len, 2, 4, &vcpu_state, nullptr),
            ZX_ERR_OUT_OF_RANGE);
  uint8_t bad_len_16bit[] = {0xc7, 0, 0x1,
                             0,    0, 0};  // opcode modrm imm4 imm3 imm2 imm1
  ASSERT_EQ(inst_decode(bad_len_16bit, 6, 2, &vcpu_state, nullptr),
            ZX_ERR_OUT_OF_RANGE);
  uint8_t bad_disp[] = {0xc7, 0b01'000'000};  // opcode modrm
  ASSERT_EQ(inst_decode(bad_disp, 2, 4, &vcpu_state, nullptr),
            ZX_ERR_OUT_OF_RANGE);
  uint8_t bad_mod_rm[] = {0xc7, 0b00'111'000, 0x1, 0, 0,
                          0};  // opcode modrm imm4 imm3 imm2 imm1
  ASSERT_EQ(inst_decode(bad_mod_rm, 6, 4, &vcpu_state, nullptr),
            ZX_ERR_INVALID_ARGS);
  uint8_t bad_h66[] = {
      0x66, 0b0100'1000, 0xc7, 0b00'000'000, 0,
      0,    0,           0x1};  // h66 rex opcode modrm imm4 imm3 imm2 imm1
  ASSERT_EQ(inst_decode(bad_h66, 8, 4, &vcpu_state, nullptr),
            ZX_ERR_NOT_SUPPORTED);

  // movl 0x1, (%rax)
  uint8_t mov[] = {0xc7, 0b00'000'000, 0x1, 0, 0,
                   0};  // opcode modrm imm4 imm3 imm2 imm1
  Instruction inst;
  ASSERT_EQ(inst_decode(mov, 6, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0x1u);
  EXPECT_EQ(inst.reg, nullptr);
  EXPECT_EQ(inst.flags, nullptr);

  // movw 0x1, (%ax)
  uint8_t mov_16bit[] = {0xc7, 0b00'000'000, 0x1, 0};  // opcode modrm imm2 imm1
  ASSERT_EQ(inst_decode(mov_16bit, 4, 2, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 2u);
  EXPECT_EQ(inst.imm, 0x1u);
  EXPECT_EQ(inst.reg, nullptr);
  EXPECT_EQ(inst.flags, nullptr);

  // movq 0x1000000, (%rax)
  uint8_t rex_mov[] = {
      0b0100'1000, 0xc7, 0b00'000'000, 0,
      0,           0,    0x1};  // rex opcode modrm imm4 imm3 imm2 imm1
  ASSERT_EQ(inst_decode(rex_mov, 7, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 8u);
  EXPECT_EQ(inst.imm, 0x1000000u);
  EXPECT_EQ(inst.reg, nullptr);
  EXPECT_EQ(inst.flags, nullptr);

  // movl 0x10, -0x1(%rbx)
  uint8_t mov_disp_1[] = {0xc7, 0b01'000'011, 0xff, 0x10, 0, 0,
                          0};  // opcode modrm disp imm4 imm3 imm2 imm1
  ASSERT_EQ(inst_decode(mov_disp_1, 7, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0x10u);
  EXPECT_EQ(inst.reg, nullptr);
  EXPECT_EQ(inst.flags, nullptr);

  // movl 0x1000000, -0x1000000(%rbx)
  uint8_t mov_disp_4[] = {
      0xc7, 0b10'000'011, 0, 0, 0, 0xff, 0, 0,
      0,    0x1};  // opcode modrm disp4 disp3 disp2 disp1 imm4 imm3 imm2 imm1
  ASSERT_EQ(inst_decode(mov_disp_4, 10, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0x1000000u);
  EXPECT_EQ(inst.reg, nullptr);
  EXPECT_EQ(inst.flags, nullptr);

  // movw 0x100, -0x1(%rax)
  uint8_t h66_mov_disp[] = {
      0x66, 0b0100'0100, 0xc7, 0b01'000'000,
      0xff, 0,           0x1};  // h66 rex opcode modrm disp imm2 imm1
  ASSERT_EQ(inst_decode(h66_mov_disp, 7, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 2u);
  EXPECT_EQ(inst.imm, 0x100u);
  EXPECT_EQ(inst.reg, nullptr);
  EXPECT_EQ(inst.flags, nullptr);

  // movl 0x10, (%rax,%rcx,2)
  uint8_t mov_sib[] = {0xc7, 0b00'000'100, 0b01'001'000, 0x10, 0, 0,
                       0};  // opcode modrm sib imm4 imm3 imm2 imm1
  ASSERT_EQ(inst_decode(mov_sib, 7, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0x10u);
  EXPECT_EQ(inst.reg, nullptr);
  EXPECT_EQ(inst.flags, nullptr);

  // movl 0x10, 0x04(%rax,%rcx,1)
  uint8_t mov_sib_disp[] = {0xc7, 0b01'000'100, 0b00'001'000, 0x04, 0x10, 0, 0,
                            0};  // opcode modrm sib disp imm4 imm3 imm2 imm1
  ASSERT_EQ(inst_decode(mov_sib_disp, 8, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0x10u);
  EXPECT_EQ(inst.reg, nullptr);
  EXPECT_EQ(inst.flags, nullptr);

  // movl 0x10, 0x00ABCDEF
  uint8_t mov_sib_nobase[] = {
      0xc7, 0b00'000'100, 0b00'100'101, 0xEF, 0xCD, 0xAB, 0x00, 0x10, 0, 0,
      0};  // opcode modrm sib disp4 disp3 disp2 disp1 imm4 imm3 imm2 imm1
  ASSERT_EQ(inst_decode(mov_sib_nobase, 11, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 4u);
  EXPECT_EQ(inst.imm, 0x10u);
  EXPECT_EQ(inst.reg, nullptr);
  EXPECT_EQ(inst.flags, nullptr);
}

// 8-bit tests to compliment decode_mov_c7.
TEST(InstDecode, mov_c6) {
  zx_vcpu_state_t vcpu_state;
  Instruction inst;

  // movb 0x1, (%rax)
  uint8_t mov[] = {0xc6, 0b00'000'000, 0x1};  // opcode modrm imm
  ASSERT_EQ(inst_decode(mov, 3, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_WRITE);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0x1u);
  EXPECT_EQ(inst.reg, nullptr);
  EXPECT_EQ(inst.flags, nullptr);
}

TEST(InstDecode, movz_0f_b6) {
  zx_vcpu_state_t vcpu_state;
  uint8_t bad_len[] = {0x0f, 0xb6, 0b00'000'000, 0};  // opcode opcode modrm ?
  ASSERT_EQ(inst_decode(bad_len, 4, 4, &vcpu_state, nullptr),
            ZX_ERR_OUT_OF_RANGE);
  uint8_t bad_disp[] = {0x0f, 0xb6, 0b01'000'000};  // opcode opcode modrm
  ASSERT_EQ(inst_decode(bad_disp, 3, 4, &vcpu_state, nullptr),
            ZX_ERR_OUT_OF_RANGE);

  // movzb (%rax), %ecx
  uint8_t movz[] = {0x0f, 0xb6, 0b00'001'000};  // opcode opcode modrm
  Instruction inst;
  ASSERT_EQ(inst_decode(movz, 3, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rcx);
  EXPECT_EQ(inst.flags, nullptr);

  // movzb (%rax), %r10d
  uint8_t rex_movz[] = {0b0100'0100, 0x0f, 0xb6,
                        0b00'010'000};  // rex opcode opcode modrm
  ASSERT_EQ(inst_decode(rex_movz, 4, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.r10);
  EXPECT_EQ(inst.flags, nullptr);

  // movzb 0x10(%rax), %ebx
  uint8_t movz_disp_1[] = {0x0f, 0xb6, 0b01'011'000,
                           0x10};  // opcode opcode modrm disp
  ASSERT_EQ(inst_decode(movz_disp_1, 4, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rbx);
  EXPECT_EQ(inst.flags, nullptr);

  // movzb 0x10000000(%rax), %ebx
  uint8_t movz_disp_4[] = {
      0x0f, 0xb6, 0b10'011'000, 0,
      0,    0,    0x1};  // opcode opcode modrm disp4 disp3 disp2 disp1
  ASSERT_EQ(inst_decode(movz_disp_4, 7, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rbx);
  EXPECT_EQ(inst.flags, nullptr);

  // movzb 0x11(rax), %r12
  uint8_t rex_movz_disp[] = {0b0100'1100, 0x0f, 0xb6, 0b01'100'000,
                             0x11};  // rex opcode opcode modrm disp
  ASSERT_EQ(inst_decode(rex_movz_disp, 5, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.r12);
  EXPECT_EQ(inst.flags, nullptr);

  // movzb (%rax),%cx
  uint8_t has_h66[] = {0x66, 0x0f, 0xb6,
                       0b00'001'000};  // h66 opcode opcode modrm
  ASSERT_EQ(inst_decode(has_h66, 4, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rcx);
  EXPECT_EQ(inst.flags, nullptr);

  // movzb (%rax),%esi
  uint8_t mov_to_esi[] = {0x0f, 0xb6, 0b00'110'000};  // opcode opcode modrm
  ASSERT_EQ(inst_decode(mov_to_esi, 3, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rsi);
  EXPECT_EQ(inst.flags, nullptr);

  // movzb (%rax,%rcx,2), %bx
  uint8_t mov_sib[] = {0x66, 0x0f, 0xb6, 0b00'011'100,
                       0b01'001'000};  // h66 opcode opcode modrm sib
  ASSERT_EQ(inst_decode(mov_sib, 5, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rbx);
  EXPECT_EQ(inst.flags, nullptr);

  // movzb 0x04(%rax,%rcx,1), %bx
  uint8_t mov_sib_disp[] = {
      0x66,         0x0f,         0xb6,
      0b01'011'100, 0b00'001'000, 0x04};  // h66 opcode opcode modrm sib disp
  ASSERT_EQ(inst_decode(mov_sib_disp, 6, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rbx);
  EXPECT_EQ(inst.flags, nullptr);

  // movzb 0xABCDEF, %ax
  uint8_t mov_sib_nobase[] = {
      0x66, 0x0f, 0xb6, 0b00'000'100, 0b00'100'101,
      0xEF, 0xCD, 0xAB, 0x00};  // h66 opcode opcode modrm sib disp4 disp3 disp2
                                // disp1
  ASSERT_EQ(inst_decode(mov_sib_nobase, 9, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rax);
  EXPECT_EQ(inst.flags, nullptr);
}

TEST(InstDecode, movz_0f_b7) {
  zx_vcpu_state_t vcpu_state;
  uint8_t bad_len[] = {0x0f, 0xb7, 0b00'000'000, 0};  // opcode opcode modrm ?
  ASSERT_EQ(inst_decode(bad_len, 4, 4, &vcpu_state, nullptr),
            ZX_ERR_OUT_OF_RANGE);
  uint8_t bad_disp[] = {0x0f, 0xb7, 0b01'000'000};  // opcode opcode modrm
  ASSERT_EQ(inst_decode(bad_disp, 3, 4, &vcpu_state, nullptr),
            ZX_ERR_OUT_OF_RANGE);
  uint8_t has_h66[] = {0x66, 0x0f, 0xb7,
                       0b00'001'000};  // h66 opcode opcode modrm
  ASSERT_EQ(inst_decode(has_h66, 4, 4, &vcpu_state, nullptr),
            ZX_ERR_BAD_STATE);

  // movzw (%rax), %ecx
  uint8_t movz[] = {0x0f, 0xb7, 0b00'001'000};  // opcode opcode modrm
  Instruction inst;
  ASSERT_EQ(inst_decode(movz, 3, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 2u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rcx);
  EXPECT_EQ(inst.flags, nullptr);

  // movzw (%rax), %cx
  uint8_t movz_16bit[] = {0x0f, 0xb7, 0b00'001'000};  // opcode opcode modrm
  ASSERT_EQ(inst_decode(movz_16bit, 3, 2, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 2u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rcx);
  EXPECT_EQ(inst.flags, nullptr);

  // movzw (%rax), %r10d
  uint8_t rex_movz[] = {0b0100'0100, 0x0f, 0xb7,
                        0b00'010'000};  // rex opcode opcode modrm
  ASSERT_EQ(inst_decode(rex_movz, 4, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 2u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.r10);
  EXPECT_EQ(inst.flags, nullptr);

  // movzw 0x10(%rax), %ebx
  uint8_t movz_disp_1[] = {0x0f, 0xb7, 0b01'011'000,
                           0x10};  // opcode opcode modrm disp
  ASSERT_EQ(inst_decode(movz_disp_1, 4, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 2u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rbx);
  EXPECT_EQ(inst.flags, nullptr);

  // movzw 0x10000000(%rax), %ebx
  uint8_t movz_disp_4[] = {
      0x0f, 0xb7, 0b10'011'000, 0,
      0,    0,    0x1};  // opcode opcode modrm disp4 disp3 disp2 disp1
  ASSERT_EQ(inst_decode(movz_disp_4, 7, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 2u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rbx);
  EXPECT_EQ(inst.flags, nullptr);

  // movzw 0x11(rax), %r12
  uint8_t rex_movz_disp[] = {0b0100'1100, 0x0f, 0xb7, 0b01'100'000,
                             0x11};  // rex opcode opcode modrm disp
  ASSERT_EQ(inst_decode(rex_movz_disp, 5, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 2u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.r12);
  EXPECT_EQ(inst.flags, nullptr);

  // movzw (%rax),%esi
  uint8_t mov_to_esi[] = {0x0f, 0xb7, 0b00'110'000};  // opcode opcode modrm
  ASSERT_EQ(inst_decode(mov_to_esi, 3, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 2u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rsi);
  EXPECT_EQ(inst.flags, nullptr);

  // movzw (%rax,%rcx,2), %ebx
  uint8_t mov_sib[] = {0x0f, 0xb7, 0b00'011'100,
                       0b01'001'000};  // opcode opcode modrm sib
  ASSERT_EQ(inst_decode(mov_sib, 4, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 2u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rbx);
  EXPECT_EQ(inst.flags, nullptr);

  // movzw 0x04(%rax,%rcx,1), %ebx
  uint8_t mov_sib_disp[] = {0x0f, 0xb7, 0b01'011'100, 0b00'001'000,
                            0x04};  // opcode opcode modrm sib disp
  ASSERT_EQ(inst_decode(mov_sib_disp, 5, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 2u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rbx);
  EXPECT_EQ(inst.flags, nullptr);

  // movzw 0x00ABCDEF, %eax
  uint8_t mov_sib_nobase[] = {
      0x0f, 0xb7, 0b00'000'100, 0b00'100'101, 0xEF,
      0xCD, 0xAB, 0x00};  // opcode opcode modrm sib disp4 disp3 disp2 disp1
  ASSERT_EQ(inst_decode(mov_sib_nobase, 8, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_MOV_READ);
  EXPECT_EQ(inst.access_size, 2u);
  EXPECT_EQ(inst.imm, 0u);
  EXPECT_EQ(inst.reg, &vcpu_state.rax);
  EXPECT_EQ(inst.flags, nullptr);
}

TEST(InstDecode, test_f6) {
  zx_vcpu_state_t vcpu_state;
  uint8_t bad_len[] = {0xf6, 0b00'000'000};  // opcode modrm
  ASSERT_EQ(inst_decode(bad_len, 2, 4, &vcpu_state, nullptr),
            ZX_ERR_OUT_OF_RANGE);
  uint8_t bad_disp[] = {0xf6, 0b01'000'000, 0};  // opcode modrm disp
  ASSERT_EQ(inst_decode(bad_disp, 3, 4, &vcpu_state, nullptr),
            ZX_ERR_OUT_OF_RANGE);
  uint8_t bad_mod_rm[] = {0xf6, 0b00'111'000, 0x1};  // opcode modrm imm
  ASSERT_EQ(inst_decode(bad_mod_rm, 3, 4, &vcpu_state, nullptr),
            ZX_ERR_INVALID_ARGS);
  uint8_t has_h66[] = {0x66, 0xf6, 0b00'001'000, 0};  // h66 opcode modrm imm
  ASSERT_EQ(inst_decode(has_h66, 4, 4, &vcpu_state, nullptr),
            ZX_ERR_BAD_STATE);

  // test 0x1, (%rax)
  uint8_t test[] = {0xf6, 0b00'000'000, 0x1};  // opcode modrm imm
  Instruction inst;
  ASSERT_EQ(inst_decode(test, 3, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_TEST);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0x1u);
  EXPECT_EQ(inst.reg, nullptr);
  EXPECT_EQ(inst.flags, &vcpu_state.rflags);

  // test 0x10, -0x1(%rbx)
  uint8_t test_disp_1[] = {0xf6, 0b01'000'011, 0xff,
                           0x10};  // opcode modrm disp imm
  ASSERT_EQ(inst_decode(test_disp_1, 4, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_TEST);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0x10u);
  EXPECT_EQ(inst.reg, nullptr);
  EXPECT_EQ(inst.flags, &vcpu_state.rflags);

  // test 0x11, -0x1000000(%rbx)
  uint8_t test_disp_4[] = {
      0xf6, 0b10'000'011, 0,   0,
      0,    0xff,         0x11};  // opcode modrm disp4 disp3 disp2 disp1 imm
  ASSERT_EQ(inst_decode(test_disp_4, 7, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_TEST);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0x11u);
  EXPECT_EQ(inst.reg, nullptr);
  EXPECT_EQ(inst.flags, &vcpu_state.rflags);

  // test 0x11, (%rax,%rcx,2)
  uint8_t test_sib[] = {0xf6, 0b00'000'100, 0b0100'1000,
                        0x11};  // opcode modrm sib imm
  ASSERT_EQ(inst_decode(test_sib, 4, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_TEST);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0x11u);
  EXPECT_EQ(inst.reg, nullptr);
  EXPECT_EQ(inst.flags, &vcpu_state.rflags);

  // test 0x11, 0x04(%rax,%rcx,1)
  uint8_t test_sib_disp[] = {0xf6, 0b01'000'100, 0b00'001'000, 0x04,
                             0x11};  // opcode modrm sib disp imm
  ASSERT_EQ(inst_decode(test_sib_disp, 5, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_TEST);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0x11u);
  EXPECT_EQ(inst.reg, nullptr);
  EXPECT_EQ(inst.flags, &vcpu_state.rflags);

  // test 0x11, 0x00ABCDEF
  uint8_t test_sib_nobase[] = {
      0xf6, 0b00'000'100, 0b00'100'101, 0xEF, 0xCD,
      0xAB, 0x00,         0x11};  // opcode modrm sib disp4 disp3 disp2 disp1
                                  // imm
  ASSERT_EQ(inst_decode(test_sib_nobase, 8, 4, &vcpu_state, &inst), ZX_OK);
  EXPECT_EQ(inst.type, INST_TEST);
  EXPECT_EQ(inst.access_size, 1u);
  EXPECT_EQ(inst.imm, 0x11u);
  EXPECT_EQ(inst.reg, nullptr);
  EXPECT_EQ(inst.flags, &vcpu_state.rflags);
}

TEST(InstDecode, computing_flags) {
  EXPECT_EQ(x86_flags_for_test8(1, 1), 2);
  EXPECT_EQ(x86_flags_for_test8(1, -1), 2);
  EXPECT_EQ(x86_flags_for_test8(-1, 1), 2);
  EXPECT_EQ(x86_flags_for_test8(3, 3), 6);
  EXPECT_EQ(x86_flags_for_test8(0, 0), 0x46);
  EXPECT_EQ(x86_flags_for_test8(-1, -1), 0x86);
}

}  // namespace
}  // namespace machina
