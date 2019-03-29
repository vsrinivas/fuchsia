// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SPN_ONCE_MACROS_GLSL
#define SPN_ONCE_MACROS_GLSL

//
// Use C-like structure layout everywhere
//
// NOTE: the current descriptors are simple enough that std430 is
// sufficient.
//

#if 0
#ifdef VULKAN
#extension GL_EXT_scalar_block_layout         : require
#endif
#endif

//
//
//

#define SPN_GLSL_INT_MIN                    (-2147483648)
#define SPN_GLSL_INT_MAX                    (+2147483647)

#define SPN_GLSL_UINT_MAX                   (4294967295)

//
//
//

#define SPN_GLSL_CONCAT_2(a,b)              a ## b
#define SPN_GLSL_CONCAT(a,b)                SPN_GLSL_CONCAT_2(a,b)

//
//
//

#define SPN_GLSL_BITS_TO_MASK(n)            ((1u<<(n))-1)
#define SPN_GLSL_BITS_TO_MASK_AT(b,n)       (SPN_GLSL_BITS_TO_MASK(n) << b)

//
// Insert a bitfield straddling the uvec2 word boundary
//

#define SPN_GLSL_INSERT_UVEC2_UINT(b,i,o,n)     \
  bitfieldInsert(b[0],i,o,32-o);                \
  bitfieldInsert(b[1],i>>(32-o),0,n-(32-o))

#define SPN_GLSL_INSERT_UVEC2_INT(b,i,o,n)      \
  SPN_GLSL_INSERT_UVEC2_UINT(b,i,o,n)

//
// Returns a uint bitfield straddling the uvec2 word boundary
// Returns an int bitfield straddling the uvec2 word boundary
//

#if defined(SPN_ENABLE_EXTENSION_INT64) && defined(GL_ARB_gpu_shader_int64)

#extension GL_ARB_gpu_shader_int64 : require

#if 1
#define SPN_GLSL_EXTRACT_UVEC2_UINT(v,o,n)  (unpackUint2x32(packUint2x32(v) >> o)[0] & SPN_GLSL_BITS_TO_MASK(n))
#else
#define SPN_GLSL_EXTRACT_UVEC2_UINT(v,o,n)  bitfieldExtract(packUint2x32(v),o,n) // might be supported
#endif

#if 1
#define SPN_GLSL_EXTRACT_UVEC2_INT(v,o,n)   bitfieldExtract(unpackInt2x32(packInt2x32(v) >> o)[0],0,n)
#else
#define SPN_GLSL_EXTRACT_UVEC2_INT(v,o,n)   bitfieldExtract(packInt2x32(v),o,n)  // might be supported
#endif

#else // int64 not enabled or supported

#define SPN_GLSL_EXTRACT_UVEC2_UINT(v,o,n)  bitfieldExtract(    (v[0] >> o) | (v[1] << (32-o)) ,0,n)
#define SPN_GLSL_EXTRACT_UVEC2_INT(v,o,n)   bitfieldExtract(int((v[0] >> o) | (v[1] << (32-o))),0,n)

#endif

//
// Certain GPUs will benefit from identifying subgroup uniform values
//

#if defined(SPN_ENABLE_EXTENSION_SUBGROUP_UNIFORM) && defined(GL_EXT_subgroupuniform_qualifier)

#extension GL_EXT_subgroupuniform_qualifier : require

#define SPN_SUBGROUP_UNIFORM                subgroupuniformEXT

#else

#define SPN_SUBGROUP_UNIFORM

#endif

//
//
//

#endif

//
//
//
