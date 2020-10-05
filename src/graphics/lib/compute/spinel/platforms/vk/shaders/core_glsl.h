// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SHADERS_CORE_GLSL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SHADERS_CORE_GLSL_H_

/////////////////////////////////////////////////////////////////
//
// VULKAN/GLSL
//

//
// clang-format off
//

#define SPN_TYPE_UINT                            uint
#define SPN_TYPE_INT                             int
#define SPN_TYPE_VEC2                            vec2
#define SPN_TYPE_VEC4                            vec4
#define SPN_TYPE_UVEC2                           uvec2
#define SPN_TYPE_UVEC4                           uvec4
#define SPN_TYPE_IVEC4                           ivec4
#define SPN_TYPE_MAT2X2                          mat2x2

#define SPN_MEMBER_UINT(name_)                   SPN_TYPE_UINT  name_
#define SPN_MEMBER_STRUCT(type,name_)            type           name_
#define SPN_MEMBER_FARRAY_UINT(name_,len_)       SPN_TYPE_UINT  name_[len_]
#define SPN_MEMBER_FARRAY_UVEC4(name_,len_)      SPN_TYPE_UVEC4 name_[len_]
#define SPN_MEMBER_VARRAY_UINT(name_)            SPN_TYPE_UINT  name_[]
#define SPN_MEMBER_VARRAY_VEC4(name_)            SPN_TYPE_VEC4  name_[]
#define SPN_MEMBER_VARRAY_UVEC2(name_)           SPN_TYPE_UVEC2 name_[]
#define SPN_MEMBER_VARRAY_UVEC4(name_)           SPN_TYPE_UVEC4 name_[]
#define SPN_MEMBER_VARRAY_STRUCT(type,name_)     type           name_[]
#define SPN_MEMBER_VARRAY_UNKNOWN(type,name_)    type           name_[]

//
//
//

#define SPN_VK_GLSL_ALIGN_GPU_SEGMENT()          layout(align = SPN_SUBGROUP_ALIGN_LIMIT)

#define SPN_VK_PUSH_UINT(name_)                  SPN_TYPE_UINT  name_;
#define SPN_VK_PUSH_UVEC4(name_)                 SPN_TYPE_UVEC4 name_;
#define SPN_VK_PUSH_IVEC4(name_)                 SPN_TYPE_IVEC4 name_;
#define SPN_VK_PUSH_UINT_FARRAY(name_,len_)      SPN_TYPE_UINT  name_[len_];
#define SPN_VK_PUSH_UINT_VARRAY(name_,len_)      SPN_TYPE_UINT  name_[len_];

//
//
//

#define SPN_VK_GLSL_PUSH(push_)                                             \
  layout(push_constant) uniform pc_ { push_ }

#define SPN_VK_GLSL_LAYOUT_BUFFER(ds_id_,s_idx_,b_idx_,name_)               \
  layout(set = s_idx_, binding = b_idx_, std430) buffer _##name_

#ifdef SPN_VK_GLSL_LAYOUT_IMAGE2D_FORMAT_IGNORED
#define SPN_VK_GLSL_LAYOUT_IMAGE2D(ds_id_,s_idx_,b_idx_,img_type_,name_)    \
  layout(set = s_idx_, binding = b_idx_) uniform writeonly image2D name_
#else
#define SPN_VK_GLSL_LAYOUT_IMAGE2D(ds_id_,s_idx_,b_idx_,img_type_,name_)    \
  layout(set = s_idx_, binding = b_idx_, img_type_) uniform writeonly image2D name_
#endif

#define SPN_VK_GLSL_BUFFER_INSTANCE(name_)                                  \
  name_

//
//
//

#define readwrite
#define noaccess

//
// Macros defined below are used in core.h
//

#define SPN_UINT_MAX                      SPN_GLSL_UINT_MAX

//
//
//

#define SPN_BITS_TO_MASK(n_)              SPN_GLSL_BITS_TO_MASK(n_)
#define SPN_BITS_TO_MASK_AT(n_,b_)        SPN_GLSL_BITS_TO_MASK_AT(n_,b_)

//
//
//

#define SPN_BITFIELD_EXTRACT(v_,o_,b_)    bitfieldExtract(v_,o_,b_)
#define SPN_BITFIELD_INSERT(v_,i_,o_,b_)  bitfieldInsert(v_,i_,o_,b_)

//
//
//

#include "core.h"
#include "spn_macros_glsl.h" // FIXME -- get rid of this and pull macros here

//
// clang-format on
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SHADERS_CORE_GLSL_H_
