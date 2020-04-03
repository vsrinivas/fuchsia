// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_HOTSORT_PLATFORMS_VK_TARGETS_HS_GLSL_MACROS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_HOTSORT_PLATFORMS_VK_TARGETS_HS_GLSL_MACROS_H_

//
// require necessary extensions -- move this downward as soon as we
// target GPUs with no support for GPUs
//

#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_shuffle : require

//
// Does this target support 64-bit shuffles?
//
// Requires: #define HS_EXT_ENABLE_SUBGROUP_EXTENDED_TYPES
//

#define HS_ENABLE_SHUFFLE_64                                                                       \
  HS_EXT_ENABLE_SUBGROUP_EXTENDED_TYPES && HS_GL_EXT_shader_subgroup_extended_types_int64

//
// Does this target support 64-bit comparisons
//
// Requires: #define HS_DISABLE_COMPARE_64  1
//

//
// sorting 64-bit keys requires additional extensions
//

#if HS_KEY_DWORDS == 2

#extension GL_EXT_shader_explicit_arithmetic_types : require

#if HS_ENABLE_SHUFFLE_64

#extension GL_EXT_shader_subgroup_extended_types_int64 : require

#endif

#endif

//
// FIXME(allanmac): restrict shouldn't have any impact on these kernels
// and benchmarks appear to prove that true but revisit this when we can
// track performance.
//

#define HS_RESTRICT restrict

//
// DEFINE THE TYPE BASED ON KEY AND VAL SIZES
//
// FIXME(allanmac): Be aware that the 64-bit key size will be updated to
// use uvec2 values on certain platforms.
//
// clang-format off
//

#if   (HS_KEY_DWORDS == 1) && (HS_VAL_DWORDS == 0)

/////////////////////

#define HS_KEY_TYPE          uint
#define HS_KEY_VAL_MAX       HS_KEY_TYPE(-1)

/////////////////////

#elif (HS_KEY_DWORDS == 2) && (HS_VAL_DWORDS == 0)

/////////////////////

#if   (!HS_DISABLE_COMPARE_64 && HS_ENABLE_SHUFFLE_64)

#define HS_KEY_TYPE          uint64_t
#define HS_KEY_VAL_MAX       HS_KEY_TYPE(-1L)

#else

#define HS_KEY_TYPE          u32vec2
#define HS_KEY_VAL_MAX       HS_KEY_TYPE(-1,-1)

#endif

/////////////////////

#else
#error "Unsupported values for HS_KEY_DWORDS and HS_VAL_DWORDS"
#endif

//
// COMPARISON SUPPORT
//

#if   (HS_KEY_DWORDS == 1) && (HS_VAL_DWORDS == 0)

/////////////////////

#define HS_KV_COMPARABLE_SRC(src_) src_
#define HS_KV_COMPARABLE_DST(dst_) dst_

/////////////////////

#elif (HS_KEY_DWORDS == 2) && (HS_VAL_DWORDS == 0)

/////////////////////

#if !HS_DISABLE_COMPARE_64

#if HS_ENABLE_SHUFFLE_64

#define HS_KV_COMPARABLE_SRC(src_) src_
#define HS_KV_COMPARABLE_DST(dst_) dst_

#else

#define HS_KV_COMPARABLE_SRC(src_) pack64(src_)
#define HS_KV_COMPARABLE_DST(dst_) unpack32(dst_)

#endif

#endif

/////////////////////

#else
#error "Unsupported values for HS_KEY_DWORDS and HS_VAL_DWORDS"
#endif

//
// SHUFFLE SUPPORT
//

#if   (HS_KEY_DWORDS == 1) && (HS_VAL_DWORDS == 0)

/////////////////////

#define HS_SUBGROUP_SHUFFLE(v, i)               \
  subgroupShuffle(v,i)

#define HS_SUBGROUP_SHUFFLE_XOR(v, m)           \
  subgroupShuffleXor(v,m)

#define HS_SUBGROUP_SHUFFLE_UP(v, d)            \
  subgroupShuffleUp(v,d)

#define HS_SUBGROUP_SHUFFLE_DOWN(v, d)          \
  subgroupShuffleDown(v,d)

/////////////////////

#elif (HS_KEY_DWORDS == 2) && (HS_VAL_DWORDS == 0)

/////////////////////

#if   (!HS_DISABLE_COMPARE_64 && HS_ENABLE_SHUFFLE_64)

#define HS_SUBGROUP_SHUFFLE(v, i)               \
  subgroupShuffle(v,i)

#define HS_SUBGROUP_SHUFFLE_XOR(v, m)           \
  subgroupShuffleXor(v,m)

#define HS_SUBGROUP_SHUFFLE_UP(v, d)            \
  subgroupShuffleUp(v,d)

#define HS_SUBGROUP_SHUFFLE_DOWN(v, d)          \
  subgroupShuffleDown(v,d)

#else // two 32-bit shuffles

#define HS_SUBGROUP_SHUFFLE(v, i)               \
  HS_KEY_TYPE(subgroupShuffle(v[0],i),          \
              subgroupShuffle(v[1],i))

#define HS_SUBGROUP_SHUFFLE_XOR(v, m)           \
  HS_KEY_TYPE(subgroupShuffleXor(v[0],m),       \
              subgroupShuffleXor(v[1],m))

#define HS_SUBGROUP_SHUFFLE_UP(v, d)            \
  HS_KEY_TYPE(subgroupShuffleUp(v[0],d),        \
              subgroupShuffleUp(v[1],d))

#define HS_SUBGROUP_SHUFFLE_DOWN(v, d)          \
  HS_KEY_TYPE(subgroupShuffleDown(v[0],d),      \
              subgroupShuffleDown(v[1],d))

#endif

/////////////////////

#else
#error "Unsupported values for HS_KEY_DWORDS and HS_VAL_DWORDS"
#endif

//
// clang-format on
//

#define HS_GLSL_WORKGROUP_SIZE(_x, _y, _z)                                                         \
  layout(local_size_x = _x, local_size_y = _y, local_size_z = _z) in

#define HS_GLSL_BINDING(_m, _n) layout(set = _m, binding = _n)

#define HS_GLSL_PUSH()                                                                             \
  layout(push_constant) uniform _pc                                                                \
  {                                                                                                \
    uint kv_offset_in;                                                                             \
    uint kv_offset_out;                                                                            \
    uint kv_count;                                                                                 \
  }

//
//
//

// clang-format off
#define HS_BUFFER_2(name_) buffer _##name_
#define HS_BUFFER(name_)   HS_BUFFER_2(name_)
// clang-format on

//
// IS HOTSORT CONFIGURED TO SORT IN PLACE?
//

#if HS_IS_IN_PLACE

//
// BS KERNEL PROTO OPERATES ON ONE BUFFER
//

// clang-format off
#ifndef HS_KV_IN
#define HS_KV_IN                   kv_inout // can be overriden
#endif
#define HS_KV_IN_LOAD(_idx)        HS_KV_IN[_idx]
#define HS_KV_IN_STORE(_idx, _kv)  HS_KV_IN[_idx]  = _kv

#ifndef HS_KV_OUT
#define HS_KV_OUT                  kv_inout // can be overriden
#endif
#define HS_KV_OUT_LOAD(_idx)       HS_KV_OUT[_idx]
#define HS_KV_OUT_STORE(_idx, _kv) HS_KV_OUT[_idx] = _kv
// clang-format on

#define HS_KV_INOUT kv_inout

#define HS_BS_KERNEL_PROTO(slab_count, slab_count_ru_log2)                                         \
  HS_GLSL_WORKGROUP_SIZE(HS_SLAB_THREADS * slab_count, 1, 1);                                      \
  HS_GLSL_BINDING(HS_KV_OUT_SET, HS_KV_OUT_BINDING) HS_BUFFER(HS_KV_INOUT)                         \
  {                                                                                                \
    HS_KEY_TYPE HS_KV_INOUT[];                                                                     \
  };                                                                                               \
  HS_GLSL_PUSH();                                                                                  \
  void main()

#else  // NOT IN PLACE

//
// BS KERNEL PROTO OPERATES ON TWO BUFFERS
//

// clang-format off
#ifndef HS_KV_IN
#define HS_KV_IN                   kv_in  // can be overriden
#endif
#define HS_KV_IN_LOAD(_idx)        HS_KV_IN[_idx]
#define HS_KV_IN_STORE(_idx, _kv)  HS_KV_IN[_idx]  = _kv

#ifndef HS_KV_OUT
#define HS_KV_OUT                  kv_out // can be overriden
#endif
#define HS_KV_OUT_LOAD(_idx)       HS_KV_OUT[_idx]
#define HS_KV_OUT_STORE(_idx, _kv) HS_KV_OUT[_idx] = _kv
// clang-format on

#define HS_BS_KERNEL_PROTO(slab_count, slab_count_ru_log2)                                         \
  HS_GLSL_WORKGROUP_SIZE(HS_SLAB_THREADS * slab_count, 1, 1);                                      \
  HS_GLSL_BINDING(HS_KV_OUT_SET, HS_KV_OUT_BINDING) writeonly HS_BUFFER(HS_KV_OUT)                 \
  {                                                                                                \
    HS_KEY_TYPE HS_KV_OUT[];                                                                       \
  };                                                                                               \
  HS_GLSL_BINDING(HS_KV_IN_SET, HS_KV_IN_BINDING) readonly HS_BUFFER(HS_KV_IN)                     \
  {                                                                                                \
    HS_KEY_TYPE HS_KV_IN[];                                                                        \
  };                                                                                               \
  HS_GLSL_PUSH();                                                                                  \
  void main()

#endif

//
// REMAINING KERNEL PROTOS ONLY OPERATE ON ONE BUFFER
//

#define HS_BC_KERNEL_PROTO(slab_count, slab_count_log2)                                            \
  HS_GLSL_WORKGROUP_SIZE(HS_SLAB_THREADS * slab_count, 1, 1);                                      \
  HS_GLSL_BINDING(HS_KV_OUT_SET, HS_KV_OUT_BINDING) HS_BUFFER(HS_KV_OUT)                           \
  {                                                                                                \
    HS_KEY_TYPE HS_KV_OUT[];                                                                       \
  };                                                                                               \
  HS_GLSL_PUSH();                                                                                  \
  void main()

#define HS_FM_KERNEL_PROTO(s, r)                                                                   \
  HS_GLSL_WORKGROUP_SIZE(HS_SLAB_THREADS, 1, 1);                                                   \
  HS_GLSL_BINDING(HS_KV_OUT_SET, HS_KV_OUT_BINDING) HS_BUFFER(HS_KV_OUT)                           \
  {                                                                                                \
    HS_KEY_TYPE HS_KV_OUT[];                                                                       \
  };                                                                                               \
  HS_GLSL_PUSH();                                                                                  \
  void main()

#define HS_HM_KERNEL_PROTO(s)                                                                      \
  HS_GLSL_WORKGROUP_SIZE(HS_SLAB_THREADS, 1, 1);                                                   \
  HS_GLSL_BINDING(HS_KV_OUT_SET, HS_KV_OUT_BINDING) HS_BUFFER(HS_KV_OUT)                           \
  {                                                                                                \
    HS_KEY_TYPE HS_KV_OUT[];                                                                       \
  };                                                                                               \
  HS_GLSL_PUSH();                                                                                  \
  void main()

#define HS_FILL_IN_KERNEL_PROTO()                                                                  \
  HS_GLSL_WORKGROUP_SIZE(HS_SLAB_THREADS, 1, 1);                                                   \
  HS_GLSL_BINDING(HS_KV_IN_SET, HS_KV_IN_BINDING) writeonly HS_BUFFER(HS_KV_IN)                    \
  {                                                                                                \
    HS_KEY_TYPE HS_KV_IN[];                                                                        \
  };                                                                                               \
  HS_GLSL_PUSH();                                                                                  \
  void main()

#define HS_FILL_OUT_KERNEL_PROTO()                                                                 \
  HS_GLSL_WORKGROUP_SIZE(HS_SLAB_THREADS, 1, 1);                                                   \
  HS_GLSL_BINDING(HS_KV_OUT_SET, HS_KV_OUT_BINDING) writeonly HS_BUFFER(HS_KV_OUT)                 \
  {                                                                                                \
    HS_KEY_TYPE HS_KV_OUT[];                                                                       \
  };                                                                                               \
  HS_GLSL_PUSH();                                                                                  \
  void main()

#define HS_TRANSPOSE_KERNEL_PROTO()                                                                \
  HS_GLSL_WORKGROUP_SIZE(HS_SLAB_THREADS, 1, 1);                                                   \
  HS_GLSL_BINDING(HS_KV_OUT_SET, HS_KV_OUT_BINDING) HS_BUFFER(HS_KV_OUT)                           \
  {                                                                                                \
    HS_KEY_TYPE HS_KV_OUT[];                                                                       \
  };                                                                                               \
  HS_GLSL_PUSH();                                                                                  \
  void main()

//
// BLOCK LOCAL MEMORY DECLARATION
//

#define HS_BLOCK_LOCAL_MEM_DECL(_width, _height)                                                   \
  shared struct                                                                                    \
  {                                                                                                \
    HS_KEY_TYPE m[_width * _height];                                                               \
  } smem

//
// BLOCK BARRIER
//

#define HS_BLOCK_BARRIER() barrier()

//
// SLAB GLOBAL
//

#define HS_SLAB_GLOBAL_BASE()                                                                      \
  const uint gmem_base = (gl_GlobalInvocationID.x & ~(HS_SLAB_THREADS - 1)) * HS_SLAB_HEIGHT

#define HS_SLAB_GLOBAL_OFFSET() const uint gmem_offset = gl_SubgroupInvocationID

//
//
//

#define HS_SLAB_GLOBAL_IDX()                                                                       \
  HS_SLAB_GLOBAL_BASE();                                                                           \
  HS_SLAB_GLOBAL_OFFSET();                                                                         \
  const uint gmem_idx = gmem_base + gmem_offset

#define HS_SLAB_GLOBAL_IDX_IN()                                                                    \
  HS_SLAB_GLOBAL_IDX();                                                                            \
  const uint gmem_in_idx = kv_offset_in + gmem_idx

#define HS_SLAB_GLOBAL_IDX_OUT()                                                                   \
  HS_SLAB_GLOBAL_IDX();                                                                            \
  const uint gmem_out_idx = kv_offset_out + gmem_idx

#define HS_SLAB_GLOBAL_IDX_IN_OUT()                                                                \
  HS_SLAB_GLOBAL_IDX();                                                                            \
  const uint gmem_in_idx  = kv_offset_in + gmem_idx;                                               \
  const uint gmem_out_idx = kv_offset_out + gmem_idx

//
//
//

// clang-format off
#define HS_SLAB_GLOBAL_LOAD_IN(_row_idx)  HS_KV_IN_LOAD( gmem_in_idx  + HS_SLAB_THREADS * _row_idx)
#define HS_SLAB_GLOBAL_LOAD_OUT(_row_idx) HS_KV_OUT_LOAD(gmem_out_idx + HS_SLAB_THREADS * _row_idx)
// clang-format on

//
//
//

#define HS_SLAB_GLOBAL_STORE_OUT(_row_idx, _kv)                                                    \
  HS_KV_OUT_STORE((gmem_out_idx + HS_SLAB_THREADS * _row_idx), _kv)

//
// SLAB LOCAL
//

#define HS_SLAB_LOCAL_L(offset) smem.m[smem_l_idx + (offset)]

#define HS_SLAB_LOCAL_R(offset) smem.m[smem_r_idx + (offset)]

//
// SLAB LOCAL VERTICAL LOADS
//

#define HS_BX_LOCAL_V(offset) smem.m[gl_LocalInvocationID.x + (offset)]

//
// BLOCK SORT MERGE HORIZONTAL
//

#define HS_BS_MERGE_H_PREAMBLE(slab_count)                                                         \
  const uint smem_l_idx =                                                                          \
    gl_SubgroupID * (HS_SLAB_THREADS * slab_count) + gl_SubgroupInvocationID;                      \
  const uint smem_r_idx = (gl_SubgroupID ^ 1) * (HS_SLAB_THREADS * slab_count) +                   \
                          (gl_SubgroupInvocationID ^ (HS_SLAB_THREADS - 1))

//
// BLOCK CLEAN MERGE HORIZONTAL
//

#define HS_BC_MERGE_H_PREAMBLE(slab_count)                                                         \
  const uint gmem_l_idx =                                                                          \
    kv_offset_out +                                                                                \
    (gl_GlobalInvocationID.x & ~(HS_SLAB_THREADS * slab_count - 1)) * HS_SLAB_HEIGHT +             \
    gl_LocalInvocationID.x;                                                                        \
  const uint smem_l_idx = gl_SubgroupID * (HS_SLAB_THREADS * slab_count) + gl_SubgroupInvocationID

#define HS_BC_GLOBAL_LOAD_L(slab_idx) HS_KV_OUT_LOAD(gmem_l_idx + (HS_SLAB_THREADS * slab_idx))

//
// SLAB FLIP AND HALF PREAMBLES
//
// NOTE(allanmac): A slab flip/half operations can use either a
// shuffle() or shuffleXor().  Try both and steer appropriately.
//

#ifndef HS_SLAB_FLIP_USE_SHUFFLE_XOR

#define HS_SLAB_FLIP_PREAMBLE(mask)                                                                \
  const uint flip_lane_idx = gl_SubgroupInvocationID ^ mask;                                       \
  const bool t_lt          = gl_SubgroupInvocationID < flip_lane_idx

#define HS_SLAB_HALF_PREAMBLE(mask)                                                                \
  const uint half_lane_idx = gl_SubgroupInvocationID ^ mask;                                       \
  const bool t_lt          = gl_SubgroupInvocationID < half_lane_idx

#else

#define HS_SLAB_FLIP_PREAMBLE(mask)                                                                \
  const uint flip_lane_mask = mask;                                                                \
  const bool t_lt           = gl_SubgroupInvocationID < (gl_SubgroupInvocationID ^ mask)

#define HS_SLAB_HALF_PREAMBLE(mask)                                                                \
  const uint half_lane_mask = mask;                                                                \
  const bool t_lt           = gl_SubgroupInvocationID < (gl_SubgroupInvocationID ^ mask)

#endif

//
// INTRA-LANE COMPARE-EXCHANGE VARIANTS
//
// Try each variant when bringing up a new target device.
//
// clang-format off
//

#if !(HS_DISABLE_COMPARE_64 && (HS_KEY_DWORDS == 2) && (HS_VAL_DWORDS == 0))

/////////////////////

// best on 32-bit keys and sometimes 64-bit keys
#define HS_CMP_XCHG_V0(a, b)                                                    \
  {                                                                             \
    const HS_KEY_TYPE t = HS_KV_COMPARABLE_DST(min(HS_KV_COMPARABLE_SRC(a),     \
                                                   HS_KV_COMPARABLE_SRC(b)));   \
    b                   = HS_KV_COMPARABLE_DST(max(HS_KV_COMPARABLE_SRC(a),     \
                                                   HS_KV_COMPARABLE_SRC(b)));   \
    a                   = t;                                                    \
  }

// ok
#define HS_CMP_XCHG_V1(a, b)                                                                       \
  {                                                                                                \
    const HS_KEY_TYPE a_prev = a;                                                                  \
    a                        = (HS_KV_COMPARABLE_SRC(a) >= HS_KV_COMPARABLE_SRC(b)) ? b : a;       \
    b                       ^= a_prev ^ a;                                                         \
  }

// sometimes best on 64-bit keys
#define HS_CMP_XCHG_V2(a, b)                                                                       \
  if (HS_KV_COMPARABLE_SRC(a) >= HS_KV_COMPARABLE_SRC(b))                                          \
    {                                                                                              \
      const HS_KEY_TYPE t = a;                                                                     \
      a                   = b;                                                                     \
      b                   = t;                                                                     \
    }

// ok
#define HS_CMP_XCHG_V3(a, b)                                                                       \
  {                                                                                                \
    const bool        ge = HS_KV_COMPARABLE_SRC(a) >= HS_KV_COMPARABLE_SRC(b);                     \
    const HS_KEY_TYPE t  = a;                                                                      \
    a                    = ge ? b : a;                                                             \
    b                    = ge ? t : b;                                                             \
  }

/////////////////////

#endif

//
// clang-format on
//

//
// A fast 64-bit comparator for platforms with no support for 64-bit integers.
//
// Try both implementations for a particular architecture and define HS_UVEC2_IS_GE.
//
// Example:
//
//   #define HS_UVEC2_IS_GE HS_UVEC2_IS_GE_V1
//

//
// A branchy implementation
//
#define HS_UVEC2_IS_GE_V0(a_, b_, ge_)                                                             \
  {                                                                                                \
    ge_ = (a_[1] > b_[1]) || ((a_[1] == b_[1]) && (a_[0] >= b_[0]));                               \
  }

//
// A branchless implementation.
//
#define HS_UVEC2_IS_GE_V1(a_, b_, ge_)                                                             \
  {                                                                                                \
    uint borrow_lo, borrow_hi, diff_hi;                                                            \
                                                                                                   \
    usubBorrow(a_[0], b_[0], borrow_lo);                                                           \
                                                                                                   \
    diff_hi = usubBorrow(a_[1], b_[1], borrow_hi);                                                 \
                                                                                                   \
    ge_ = (((diff_hi == 0) ? borrow_lo : borrow_hi) == 0);                                         \
  }

//
// INTRA-LANE COMPARE-EXCHANGE VARIANTS (UVEC2)
//
// Try each variant when bringing up a new target device.
//

#define HS_UVEC2_CMP_XCHG_V0(a, b)                                                                 \
  {                                                                                                \
    bool ge;                                                                                       \
                                                                                                   \
    HS_UVEC2_IS_GE(a, b, ge)                                                                       \
                                                                                                   \
    if (ge)                                                                                        \
      {                                                                                            \
        const HS_KEY_TYPE t = a;                                                                   \
        a                   = b;                                                                   \
        b                   = t;                                                                   \
      }                                                                                            \
  }

#define HS_UVEC2_CMP_XCHG_V1(a, b)                                                                 \
  {                                                                                                \
    bool ge;                                                                                       \
                                                                                                   \
    HS_UVEC2_IS_GE(a, b, ge)                                                                       \
                                                                                                   \
    const HS_KEY_TYPE t = a;                                                                       \
    a                   = ge ? b : a;                                                              \
    b                   = ge ? t : b;                                                              \
  }

//
// INTER-LANE COMPARE-EXCHANGE VARIANTS
//
// If the shuffled lane is greater than the current lane return min(a,b)
// If the shuffled lane is less    than the current lane return max(a,b)
//

#define HS_LOGICAL_XOR ^^  // the clang formatter mangles ^^

#if !(HS_DISABLE_COMPARE_64 && (HS_KEY_DWORDS == 2) && (HS_VAL_DWORDS == 0))

/////////////////////

// this is the default
#define HS_COND_MIN_MAX_V0(lt, a, b)                                                               \
  {                                                                                                \
    const bool ge    = (HS_KV_COMPARABLE_SRC(a) >= HS_KV_COMPARABLE_SRC(b));                       \
    const bool ge_lt = (ge HS_LOGICAL_XOR lt);                                                     \
    a                = ge_lt ? a : b;                                                              \
  }

// this is about the same but a little more divergent
#define HS_COND_MIN_MAX_V1(lt, a, b)                                                               \
  {                                                                                                \
    if (lt)                                                                                        \
      {                                                                                            \
        a = HS_KV_COMPARABLE_DST(min(HS_KV_COMPARABLE_SRC(a), HS_KV_COMPARABLE_SRC(b)));           \
      }                                                                                            \
    else                                                                                           \
      {                                                                                            \
        a = HS_KV_COMPARABLE_DST(max(HS_KV_COMPARABLE_SRC(a), HS_KV_COMPARABLE_SRC(b)));           \
      }                                                                                            \
  }

/////////////////////

#endif

//
// INTER-LANE COMPARE-EXCHANGE VARIANTS (UVEC2)
//

// this is the default
#define HS_UVEC2_COND_MIN_MAX_V0(lt, a, b)                                                         \
  {                                                                                                \
    bool ge;                                                                                       \
                                                                                                   \
    HS_UVEC2_IS_GE(a, b, ge)                                                                       \
                                                                                                   \
    const bool ge_lt = (ge HS_LOGICAL_XOR lt);                                                     \
    a                = ge_lt ? a : b;                                                              \
  }

//
// INTER-LANE FLIP/HALF COMPARE-EXCHANGE
//

#ifndef HS_SLAB_FLIP_USE_SHUFFLE_XOR

#define HS_CMP_FLIP(i, a, b)                                                                       \
  {                                                                                                \
    const HS_KEY_TYPE ta = HS_SUBGROUP_SHUFFLE(a, flip_lane_idx);                                  \
    const HS_KEY_TYPE tb = HS_SUBGROUP_SHUFFLE(b, flip_lane_idx);                                  \
    HS_COND_MIN_MAX(t_lt, a, tb);                                                                  \
    HS_COND_MIN_MAX(t_lt, b, ta);                                                                  \
  }

#define HS_CMP_HALF(i, a)                                                                          \
  {                                                                                                \
    const HS_KEY_TYPE ta = HS_SUBGROUP_SHUFFLE(a, half_lane_idx);                                  \
    HS_COND_MIN_MAX(t_lt, a, ta);                                                                  \
  }

#else  // uses shuffle xor

#define HS_CMP_FLIP(i, a, b)                                                                       \
  {                                                                                                \
    const HS_KEY_TYPE ta = HS_SUBGROUP_SHUFFLE_XOR(a, flip_lane_mask);                             \
    const HS_KEY_TYPE tb = HS_SUBGROUP_SHUFFLE_XOR(b, flip_lane_mask);                             \
    HS_COND_MIN_MAX(t_lt, a, tb);                                                                  \
    HS_COND_MIN_MAX(t_lt, b, ta);                                                                  \
  }

#define HS_CMP_HALF(i, a)                                                                          \
  {                                                                                                \
    const HS_KEY_TYPE ta = HS_SUBGROUP_SHUFFLE_XOR(a, half_lane_mask);                             \
    HS_COND_MIN_MAX(t_lt, a, ta);                                                                  \
  }

#endif

//
// The "flip-merge" and "half-merge" preambles are very similar
//
// For now, we're only using the .y dimension for the span idx
//

#define HS_HM_PREAMBLE(half_span)                                                                  \
  const uint span_idx    = gl_WorkGroupID.y;                                                       \
  const uint span_stride = gl_NumWorkGroups.x * gl_WorkGroupSize.x;                                \
  const uint span_size   = span_stride * half_span * 2;                                            \
  const uint span_base   = kv_offset_out + span_idx * span_size;                                   \
  const uint span_off    = gl_GlobalInvocationID.x;                                                \
  const uint span_l      = span_base + span_off

#define HS_FM_PREAMBLE(half_span)                                                                  \
  HS_HM_PREAMBLE(half_span);                                                                       \
  const uint span_r = span_base + span_stride * (half_span + 1) - span_off - 1

//
//
//

// clang-format off
#define HS_XM_GLOBAL_L(stride_idx)            (span_l + span_stride * stride_idx)
#define HS_XM_GLOBAL_LOAD_L(stride_idx)       HS_KV_OUT_LOAD(HS_XM_GLOBAL_L(stride_idx))
#define HS_XM_GLOBAL_STORE_L(stride_idx, reg) HS_KV_OUT_STORE(HS_XM_GLOBAL_L(stride_idx), reg)

#define HS_FM_GLOBAL_R(stride_idx)            (span_r + span_stride * stride_idx)
#define HS_FM_GLOBAL_LOAD_R(stride_idx)       HS_KV_OUT_LOAD(HS_FM_GLOBAL_R(stride_idx))
#define HS_FM_GLOBAL_STORE_R(stride_idx, reg) HS_KV_OUT_STORE(HS_FM_GLOBAL_R(stride_idx), reg)
// clang-format on

//
// TODO/OPTIMIZATION:
//
// Use of signed indices and signed constant offsets may positively
// impact load/store instruction selection.
//

#define HS_FILL_IN_BODY()                                                                          \
  HS_SLAB_GLOBAL_BASE();                                                                           \
  HS_SLAB_GLOBAL_OFFSET();                                                                         \
  if (gmem_base >= kv_count)                                                                       \
    {                                                                                              \
      const uint gmem_in_idx = kv_offset_in + gmem_base + gmem_offset;                             \
                                                                                                   \
      for (uint ii = 0; ii < HS_SLAB_HEIGHT; ii++)                                                 \
        {                                                                                          \
          HS_KV_IN_STORE(gmem_in_idx + ii * HS_SLAB_WIDTH, HS_KEY_VAL_MAX);                        \
        }                                                                                          \
    }                                                                                              \
  else                                                                                             \
    {                                                                                              \
      const uint kv_count_base = kv_count & ~(HS_SLAB_THREADS - 1);                                \
      uint       gmem_in_idx   = kv_offset_in + kv_count_base + gmem_offset;                       \
                                                                                                   \
      if (gmem_offset >= (kv_count & (HS_SLAB_THREADS - 1)))                                       \
        {                                                                                          \
          HS_KV_IN_STORE(gmem_in_idx, HS_KEY_VAL_MAX);                                             \
        }                                                                                          \
                                                                                                   \
      gmem_in_idx += HS_SLAB_THREADS;                                                              \
                                                                                                   \
      const uint gmem_in_max = kv_offset_in + gmem_base + (HS_SLAB_THREADS * HS_SLAB_HEIGHT);      \
                                                                                                   \
      for (; gmem_in_idx < gmem_in_max; gmem_in_idx += HS_SLAB_THREADS)                            \
        {                                                                                          \
          HS_KV_IN_STORE(gmem_in_idx, HS_KEY_VAL_MAX);                                             \
        }                                                                                          \
    }

#define HS_FILL_OUT_BODY()                                                                         \
  HS_SLAB_GLOBAL_IDX();                                                                            \
  const uint gmem_out_idx = kv_offset_out + gmem_idx;                                              \
  for (uint ii = 0; ii < HS_SLAB_HEIGHT; ii++)                                                     \
    {                                                                                              \
      HS_KV_OUT_STORE(gmem_out_idx + ii * HS_SLAB_THREADS, HS_KEY_VAL_MAX);                        \
    }

//
// This snarl of macros is for transposing a serpentine-ordered "slab"
// of sorted elements into linear order.
//
// This can occur as the last step in hs_sort() or via a custom kernel
// that inspects the slab and then transposes and stores it to memory.
//
// The slab format can be inspected more efficiently than a linear
// arrangement so is useful for post-sort operations.
//
// The prime example is detecting when adjacent keys (in sort order)
// have differing high order bits ("key changes").  The index of each
// change is recorded to an auxilary array.
//
// A post-processing step like this needs to be able to navigate the
// slab and eventually transpose and store the slab in linear order.
//

// clang-format off
#define HS_TRANSPOSE_REG(prefix, row)  prefix##row
#define HS_TRANSPOSE_DECL(prefix, row) const HS_KEY_TYPE HS_TRANSPOSE_REG(prefix, row)
#define HS_TRANSPOSE_PRED(level)       is_lo_##level
// clang-format on

//
//
//

#define HS_TRANSPOSE_TMP_SEL_REG(prefix_curr, row_ll, row_ur) prefix_curr##row_ll##_##row_ur##_sel

#define HS_TRANSPOSE_TMP_SEL_DECL(prefix_curr, level, row_ll, row_ur)                              \
  const HS_KEY_TYPE HS_TRANSPOSE_TMP_SEL_REG(prefix_curr, row_ll, row_ur)

#define HS_TRANSPOSE_TMP_REG(prefix_curr, row_ll, row_ur) prefix_curr##row_ll##_##row_ur

#define HS_TRANSPOSE_TMP_DECL(prefix_curr, row_ll, row_ur)                                         \
  const HS_KEY_TYPE HS_TRANSPOSE_TMP_REG(prefix_curr, row_ll, row_ur)

#define HS_TRANSPOSE_STAGE(level)                                                                  \
  const bool HS_TRANSPOSE_PRED(level) = (gl_SubgroupInvocationID & (1 << (level - 1))) == 0;

//
//
//

#define HS_TRANSPOSE_BLEND(prefix_prev, prefix_curr, level, row_ll, row_ur)                        \
                                                                                                   \
  HS_TRANSPOSE_TMP_SEL_DECL(prefix_curr, level, row_ll, row_ur) =                                  \
    HS_TRANSPOSE_PRED(level) ? HS_TRANSPOSE_REG(prefix_prev, row_ll)                               \
                             : HS_TRANSPOSE_REG(prefix_prev, row_ur);                              \
                                                                                                   \
  HS_TRANSPOSE_TMP_DECL(prefix_curr, row_ll, row_ur) =                                             \
    HS_SUBGROUP_SHUFFLE_XOR(HS_TRANSPOSE_TMP_SEL_REG(prefix_curr, row_ll, row_ur),                 \
                            1 << (level - 1));                                                     \
                                                                                                   \
  HS_TRANSPOSE_DECL(prefix_curr, row_ll) = HS_TRANSPOSE_PRED(level)                                \
                                             ? HS_TRANSPOSE_TMP_REG(prefix_curr, row_ll, row_ur)   \
                                             : HS_TRANSPOSE_REG(prefix_prev, row_ll);              \
                                                                                                   \
  HS_TRANSPOSE_DECL(prefix_curr, row_ur) = HS_TRANSPOSE_PRED(level)                                \
                                             ? HS_TRANSPOSE_REG(prefix_prev, row_ur)               \
                                             : HS_TRANSPOSE_TMP_REG(prefix_curr, row_ll, row_ur);

//
//
//

#define HS_TRANSPOSE_REMAP(prefix, row_from, row_to)                                               \
  HS_KV_OUT_STORE(gmem_out_idx + ((row_to - 1) << HS_SLAB_WIDTH_LOG2),                             \
                  HS_TRANSPOSE_REG(prefix, row_from));

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_HOTSORT_PLATFORMS_VK_TARGETS_HS_GLSL_MACROS_H_
