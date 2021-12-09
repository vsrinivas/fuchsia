// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_SHADERS_MACROS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_SHADERS_MACROS_H_

/////////////////////////////////////////////////////////////////
//
// GLSL
//

//
// Literals
//

#define UINT32_MAX 0xFFFFFFFF

//
//
//

#ifdef SPN_VK_GLSL_LAYOUT_IMAGE2D_FORMAT_IGNORED
#define SPN_VK_GLSL_LAYOUT_IMAGE2D(ds_id_, s_idx_, b_idx_, img_type_, name_)                       \
  layout(set = s_idx_, binding = b_idx_) uniform writeonly image2D name_
#else
#define SPN_VK_GLSL_LAYOUT_IMAGE2D(ds_id_, s_idx_, b_idx_, img_type_, name_)                       \
  layout(set = s_idx_, binding = b_idx_, img_type_) uniform writeonly image2D name_
#endif

//
// clang-format off
//

//
// Macros defined below are used in core.h
//

#define SPN_BITS_TO_MASK(n_)                   SPN_GLSL_BITS_TO_MASK(n_)
#define SPN_BITS_TO_MASK_AT(n_,b_)             SPN_GLSL_BITS_TO_MASK_AT(n_,b_)

//
//
//

#define SPN_BITFIELD_EXTRACT(v_,o_,b_)         bitfieldExtract(v_,o_,b_)
#define SPN_BITFIELD_INSERT(v_,i_,o_,b_)       bitfieldInsert(v_,i_,o_,b_)

//
// FIXME
//
// Consider providing typed min/max() functions:
//
//   <type> [min|max]_<type>(a,b) { ; }
//
// But note we still need preprocessor/compile-time min/max().
//

#define SPN_GLSL_MAX_MACRO(t_,a_,b_)           (((a_) > (b_)) ? (a_) : (b_))
#define SPN_GLSL_MIN_MACRO(t_,a_,b_)           (((a_) < (b_)) ? (a_) : (b_))

//
//
//

#define SPN_GLSL_CONCAT_2(a_,b_)               a_ ## b_
#define SPN_GLSL_CONCAT(a_,b_)                 SPN_GLSL_CONCAT_2(a_,b_)

//
//
//

#define SPN_GLSL_BITS_TO_MASK(n_)              ((1u<<(n_))-1)
#define SPN_GLSL_BITS_TO_MASK_AT(b_,n_)        (SPN_GLSL_BITS_TO_MASK(n_) << (b_))

//
// Insert a bitfield straddling the uvec2 word boundary
//
// NOTE: 64-bit inserts, extracts and rotates are operations we want to
// accelerate with intrinsics when available through GLSL. For example,
// NVIDIA's 64-bit SHF opcode.
//

#define SPN_GLSL_INSERT_UVEC2_COMMON(b_,i_,o_,lo_,hi_)          \
  b_[0] = SPN_BITFIELD_INSERT((b_)[0],i_,o_,lo_);               \
  b_[1] = SPN_BITFIELD_INSERT((b_)[1],                          \
                              SPN_BITFIELD_EXTRACT(i_,lo_,hi_), \
                              0,                                \
                              hi_)

#define SPN_GLSL_INSERT_UVEC2_UINT(b_,i_,o_,n_)                 \
  SPN_GLSL_INSERT_UVEC2_COMMON(b_,i_,o_,32-(o_),(n_)-(32-(o_)))

#define SPN_GLSL_INSERT_UVEC2_INT(b_,i_,o_,n_)                  \
  SPN_GLSL_INSERT_UVEC2_UINT(b_,i_,o_,n_)

//
// Returns a uint bitfield straddling the uvec2 word boundary
// Returns an int bitfield straddling the uvec2 word boundary
//

#if (SPN_EXT_ENABLE_INT64 && GL_EXT_shader_explicit_arithmetic_types)

#extension GL_EXT_shader_explicit_arithmetic_types : require

#if 1
#define SPN_GLSL_EXTRACT_UVEC2_UINT(v_,o_,n_)  (unpack32(pack64(v_) >> o_)[0] & SPN_GLSL_BITS_TO_MASK(n_))
#else
#define SPN_GLSL_EXTRACT_UVEC2_UINT(v_,o_,n_)  SPN_BITFIELD_EXTRACT(pack64(v_),o_,n_) // might be supported
#endif

#if 1
#define SPN_GLSL_EXTRACT_UVEC2_INT(v_,o_,n_)   SPN_BITFIELD_EXTRACT(unpack32(pack64(v_) >> o_)[0],0,n_)
#else
#define SPN_GLSL_EXTRACT_UVEC2_INT(v_,o_,n_)   SPN_BITFIELD_EXTRACT(pack64(v_),o_,n_)  // might be supported
#endif

#else // int64 not enabled or supported

#define SPN_GLSL_EXTRACT_UVEC2_UINT(v_,o_,n_)  SPN_BITFIELD_EXTRACT(    ((v_)[0] >> (o_)) | ((v_)[1] << (32-(o_))) ,0,n_)
#define SPN_GLSL_EXTRACT_UVEC2_INT(v_,o_,n_)   SPN_BITFIELD_EXTRACT(int(((v_)[0] >> (o_)) | ((v_)[1] << (32-(o_)))),0,n_)

#endif

//
// GPUs with support for true scalars will benefit from identifying
// subgroup uniform values
//

#if SPN_EXT_ENABLE_SUBGROUP_UNIFORM && GL_EXT_subgroupuniform_qualifier

#extension GL_EXT_subgroupuniform_qualifier : require

#define SPN_SUBGROUP_UNIFORM  subgroupuniformEXT

#else

#define SPN_SUBGROUP_UNIFORM

#endif

//
// clang-format on
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_SHADERS_MACROS_H_
