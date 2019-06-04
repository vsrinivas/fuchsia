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

#define SPN_MEMBER_UINT(name)                    SPN_TYPE_UINT  name
#define SPN_MEMBER_STRUCT(type,name)             type           name
#define SPN_MEMBER_FARRAY_UINT(name,len)         SPN_TYPE_UINT  name[len]
#define SPN_MEMBER_VARRAY_UINT(name)             SPN_TYPE_UINT  name[]
#define SPN_MEMBER_VARRAY_VEC4(name)             SPN_TYPE_VEC4  name[]
#define SPN_MEMBER_VARRAY_UVEC2(name)            SPN_TYPE_UVEC2 name[]
#define SPN_MEMBER_VARRAY_UVEC4(name)            SPN_TYPE_UVEC4 name[]
#define SPN_MEMBER_VARRAY_STRUCT(type,name)      type           name[]
#define SPN_MEMBER_VARRAY_UNKNOWN(type,name)     type           name[]

//
//
//

#define SPN_VK_GLSL_MQ_RW                 // read and write by shader
#define SPN_VK_GLSL_MQ_NOACCESS           // no access by shader

#define SPN_VK_GLSL_ALIGN()               layout(align = SPN_SUBGROUP_ALIGN_LIMIT)

#define SPN_VK_PUSH_UINT(name)            SPN_TYPE_UINT  name;
#define SPN_VK_PUSH_UVEC4(name)           SPN_TYPE_UVEC4 name;
#define SPN_VK_PUSH_IVEC4(name)           SPN_TYPE_IVEC4 name;
#define SPN_VK_PUSH_UINT_FARRAY(name,len) SPN_TYPE_UINT  name[len];
#define SPN_VK_PUSH_UINT_VARRAY(name,len) SPN_TYPE_UINT  name[len];

//
//
//

#define SPN_VK_GLSL_PUSH(_push)                                             \
  layout(push_constant) uniform _pc { _push }

#define SPN_VK_GLSL_LAYOUT_BUFFER(_ds_id,_s_idx,_b_idx,_name)               \
  layout(set = _s_idx, binding = _b_idx, std430) buffer _##_name

#define SPN_VK_GLSL_LAYOUT_IMAGE2D(_ds_id,_s_idx,_b_idx,_img_type,_name)    \
  layout(set = _s_idx, binding = _b_idx, _img_type) uniform image2D _##_name

#define SPN_VK_GLSL_BUFFER_INSTANCE(_name)                                  \
  _name

//
//
//

#define readwrite
#define noaccess

//
// Macros defined below are used in core.h
//

#define SPN_UINT_MAX                 SPN_GLSL_UINT_MAX

//
//
//

#define SPN_BITS_TO_MASK(n)          SPN_GLSL_BITS_TO_MASK(n)
#define SPN_BITS_TO_MASK_AT(n,b)     SPN_GLSL_BITS_TO_MASK_AT(n,b)

//
//
//

#define SPN_BITFIELD_EXTRACT(v,o,b)  bitfieldExtract(v,o,b)
#define SPN_BITFIELD_INSERT(v,i,o,b) bitfieldInsert(v,i,o,b)

//
//
//

#include "core.h"
#include "spn_macros_glsl.h"

//
// clang-format on
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SHADERS_CORE_GLSL_H_
