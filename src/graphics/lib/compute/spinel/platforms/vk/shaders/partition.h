// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SHADERS_PARTITION_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SHADERS_PARTITION_H_

//
//
//

#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_ballot : require

//
// Note that NVIDIA provides the PTX instruction "match" on sm_70
// devices.
//
// This instruction returns a ballot of all matching lanes in a
// subgroup.
//
// The GLSL instruction is available via the
// GL_NV_shader_subgroup_partitioned extension:
//
//   uvec4 subgroupPartitionNV()
//
// Pre-sm_70 NVIDIA devices emulate the instruction.
//
// On non-NVIDIA platforms, this primitive can also be emulated.
//
// FIXME(allanmac): Investigate if improving these primitives is
// possible... I'm not sure if there is a better implementation than the
// alternatives listed below?
//

//
// Partition using subgroup broadcasts.
//
// Note: These are defined as macros to avoid compilation and promote
// customization.
//

#define SPN_PARTITION_BROADCAST_INIT(part_, v_)                                                    \
  part_ = uvec4((subgroupBroadcast((v_), 0) == (v_)) ? 1 : 0, 0, 0, 0)

#define SPN_PARTITION_BROADCAST_TEST(part_, v_, i_)                                                \
  part_[(i_) / 32] |= (subgroupBroadcast((v_), (i_)) == (v_)) ? (1u << ((i_)&0x1F)) : 0

#define SPN_PARTITION_BROADCAST_4(part_, v_)                                                       \
  SPN_PARTITION_BROADCAST_INIT(part_, (v_));                                                       \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x01);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x02);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x03);

#define SPN_PARTITION_BROADCAST_8(part_, v_)                                                       \
  SPN_PARTITION_BROADCAST_4(part_, (v_))                                                           \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x04);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x05);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x06);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x07);

#define SPN_PARTITION_BROADCAST_16(part_, v_)                                                      \
  SPN_PARTITION_BROADCAST_8(part_, (v_))                                                           \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x08);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x09);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x0A);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x0B);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x0C);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x0D);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x0E);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x0F);

#define SPN_PARTITION_BROADCAST_32(part_, v_)                                                      \
  SPN_PARTITION_BROADCAST_16(part_, (v_))                                                          \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x10);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x11);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x12);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x13);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x14);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x15);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x16);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x17);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x18);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x19);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x1A);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x1B);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x1C);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x1D);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x1E);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x1F);

#define SPN_PARTITION_BROADCAST_64(part_, v_)                                                      \
  SPN_PARTITION_BROADCAST_32(part_, (v_))                                                          \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x20);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x21);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x22);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x23);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x24);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x25);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x26);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x27);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x28);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x29);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x2A);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x2B);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x2C);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x2D);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x2E);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x2F);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x30);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x31);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x32);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x33);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x34);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x35);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x36);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x37);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x38);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x39);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x3A);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x3B);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x3C);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x3D);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x3E);                                                 \
  SPN_PARTITION_BROADCAST_TEST(part_, (v_), 0x3F);

//
// Partition the subgroup of value one bit at a time.
//
// This implementation may be preferable on wide subgroup devices when
// partitioning a small number of bits.
//
// Note that unused dwords in the ballot aren't initialized.
//

// clang-format off
#define SPN_PARTITION_BALLOT_UVEC1(vec_) (vec_).x
#define SPN_PARTITION_BALLOT_UVEC2(vec_) (vec_).xy

#define SPN_PARTITION_BALLOT_ONE         uvec4(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF)
#define SPN_PARTITION_BALLOT_ZERO        uvec4(0,          0,          0,          0)
// clang-format on

#define SPN_PARTITION_BALLOT_INIT(w_, part_, v_, i_, lo_, hi_)                                     \
  {                                                                                                \
    const bool p = ((v_) & (((1u << (1 + (hi_) - (lo_))) - 1) << (i_))) != 0;                      \
                                                                                                   \
    w_(part_) =                                                                                    \
      w_(subgroupBallot(p)) ^ (p ? w_(SPN_PARTITION_BALLOT_ZERO) : w_(SPN_PARTITION_BALLOT_ONE));  \
  }

#define SPN_PARTITION_BALLOT_TEST(w_, part_, v_, i_, lo_, hi_)                                     \
  {                                                                                                \
    const bool p = ((v_) & (((1u << (1 + (hi_) - (lo_))) - 1) << (i_))) != 0;                      \
                                                                                                   \
    w_(part_) &=                                                                                   \
      w_(subgroupBallot(p)) ^ (p ? w_(SPN_PARTITION_BALLOT_ZERO) : w_(SPN_PARTITION_BALLOT_ONE));  \
  }

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SHADERS_PARTITION_H_
