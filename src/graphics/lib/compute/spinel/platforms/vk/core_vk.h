// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_CORE_VK_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_CORE_VK_H_

/////////////////////////////////////////////////////////////////
//
// C99/VULKAN
//

//
// clang-format off
//

#define SPN_MEMBER_UINT(name)                    SPN_TYPE_UINT  name
#define SPN_MEMBER_STRUCT(type,name)             struct type    name
#define SPN_MEMBER_FARRAY_UINT(name,len)         SPN_TYPE_UINT  name[len]
#define SPN_MEMBER_VARRAY_UINT(name)             SPN_TYPE_UINT  name[0]
#define SPN_MEMBER_VARRAY_VEC4(name)             SPN_TYPE_VEC4  name[0]
#define SPN_MEMBER_VARRAY_UVEC2(name)            SPN_TYPE_UVEC2 name[0]
#define SPN_MEMBER_VARRAY_UVEC4(name)            SPN_TYPE_UVEC4 name[0]
#define SPN_MEMBER_VARRAY_STRUCT(type,name)      struct type    name[0]
#define SPN_MEMBER_VARRAY_UNKNOWN(type,name)     uint8_t        name[0]

//
//
//

#define SPN_VK_TARGET_GLSL_ALIGN()               ALIGN_MACRO(SPN_SUBGROUP_ALIGN_LIMIT)

#define SPN_VK_TARGET_PUSH_UINT(name)            SPN_TYPE_UINT  name;
#define SPN_VK_TARGET_PUSH_UVEC4(name)           SPN_TYPE_UVEC4 name;
#define SPN_VK_TARGET_PUSH_IVEC4(name)           SPN_TYPE_IVEC4 name;
#define SPN_VK_TARGET_PUSH_UINT_FARRAY(name,len) SPN_TYPE_UINT  name[len];
#define SPN_VK_TARGET_PUSH_UINT_VARRAY(name,len) SPN_TYPE_UINT  name[];

//
//
//

#include "core_c.h"

//
// clang-format on
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_CORE_VK_H_
