// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HS_GLSL_MACROS_ONCE
#define HS_GLSL_MACROS_ONCE

//
// Define the type based on key and val sizes
//

#if   HS_KEY_DWORDS == 1
#if   HS_VAL_DWORDS == 0
#define HS_KEY_TYPE    uint
#define HS_KEY_VAL_MAX HS_KEY_TYPE(-1)
#endif
#elif HS_KEY_DWORDS == 2       // FIXME -- might want to use uint2
#if   HS_VAL_DWORDS == 0
#define HS_KEY_TYPE    uint64_t // GL_ARB_gpu_shader_int64
#define HS_KEY_VAL_MAX HS_KEY_TYPE(-1L)
#endif
#endif

//
// FYI, restrict shouldn't have any impact on these kernels and
// benchmarks appear to prove that true
//

#define HS_RESTRICT restrict

//
//
//

#define HS_GLSL_WORKGROUP_SIZE(_x,_y,_z)         \
  layout( local_size_x = _x,                     \
          local_size_y = _y,                     \
          local_size_z = _z) in

#define HS_GLSL_BINDING(_m,_n)                  \
  layout(set = _m, binding = _n)

#define HS_GLSL_PUSH()                          \
  layout(push_constant)                         \
    uniform _pc {                               \
    uint kv_offset_in;                          \
    uint kv_offset_out;                         \
    uint kv_count;                              \
  }

//
// These can be overidden
//

#define HS_KV_IN                  kv_in
#define HS_KV_OUT                 kv_out

//
//
//

#define HS_KV_IN_LOAD(_idx)       HS_KV_IN [_idx]
#define HS_KV_OUT_LOAD(_idx)      HS_KV_OUT[_idx]

#define HS_KV_IN_STORE(_idx,_kv)  HS_KV_IN [_idx] = _kv
#define HS_KV_OUT_STORE(_idx,_kv) HS_KV_OUT[_idx] = _kv

//
// KERNEL PROTOS
//

#define HS_BS_KERNEL_PROTO(slab_count,slab_count_ru_log2)                       \
  HS_GLSL_SUBGROUP_SIZE()                                                       \
  HS_GLSL_WORKGROUP_SIZE(HS_SLAB_THREADS*slab_count,1,1);                       \
  HS_GLSL_BINDING(0,0) writeonly buffer _kv_out { HS_KEY_TYPE HS_KV_OUT[]; };   \
  HS_GLSL_BINDING(0,1) readonly  buffer _kv_in  { HS_KEY_TYPE HS_KV_IN[];  };   \
  HS_GLSL_PUSH();                                                               \
  void main()

#define HS_BC_KERNEL_PROTO(slab_count,slab_count_log2)                  \
  HS_GLSL_SUBGROUP_SIZE()                                               \
  HS_GLSL_WORKGROUP_SIZE(HS_SLAB_THREADS*slab_count,1,1);               \
  HS_GLSL_BINDING(0,0) buffer _kv_out { HS_KEY_TYPE HS_KV_OUT[]; };     \
  HS_GLSL_PUSH();                                                       \
  void main()

#define HS_FM_KERNEL_PROTO(s,r)                                         \
  HS_GLSL_SUBGROUP_SIZE()                                               \
  HS_GLSL_WORKGROUP_SIZE(HS_SLAB_THREADS,1,1);                          \
  HS_GLSL_BINDING(0,0) buffer _kv_out { HS_KEY_TYPE HS_KV_OUT[]; };     \
  HS_GLSL_PUSH();                                                       \
  void main()

#define HS_HM_KERNEL_PROTO(s)                                           \
  HS_GLSL_SUBGROUP_SIZE()                                               \
  HS_GLSL_WORKGROUP_SIZE(HS_SLAB_THREADS,1,1);                          \
  HS_GLSL_BINDING(0,0) buffer _kv_out { HS_KEY_TYPE HS_KV_OUT[]; };     \
  HS_GLSL_PUSH();                                                       \
  void main()

#define HS_FILL_IN_KERNEL_PROTO()                                               \
  HS_GLSL_SUBGROUP_SIZE()                                                       \
  HS_GLSL_WORKGROUP_SIZE(HS_SLAB_THREADS,1,1);                                  \
  HS_GLSL_BINDING(0,1) writeonly buffer _kv_in { HS_KEY_TYPE HS_KV_IN[]; };     \
  HS_GLSL_PUSH();                                                               \
  void main()

#define HS_FILL_OUT_KERNEL_PROTO()                                              \
  HS_GLSL_SUBGROUP_SIZE()                                                       \
  HS_GLSL_WORKGROUP_SIZE(HS_SLAB_THREADS,1,1);                                  \
  HS_GLSL_BINDING(0,0) writeonly buffer _kv_out { HS_KEY_TYPE HS_KV_OUT[]; };   \
  HS_GLSL_PUSH();                                                               \
  void main()

#define HS_TRANSPOSE_KERNEL_PROTO()                                     \
  HS_GLSL_SUBGROUP_SIZE()                                               \
  HS_GLSL_WORKGROUP_SIZE(HS_SLAB_THREADS,1,1);                          \
  HS_GLSL_BINDING(0,0) buffer _kv_out { HS_KEY_TYPE HS_KV_OUT[]; };     \
  HS_GLSL_PUSH();                                                       \
  void main()

//
// BLOCK LOCAL MEMORY DECLARATION
//

#define HS_BLOCK_LOCAL_MEM_DECL(_width,_height) \
  shared struct {                               \
    HS_KEY_TYPE m[_width * _height];            \
  } smem

//
// BLOCK BARRIER
//

#define HS_BLOCK_BARRIER()  barrier()

//
// SHUFFLES
//

//
// Note that not all target architectures have support for uint64.
// For this reason we will probably switch to a uvec2 representation
// for 8-byte key-vals.
//

#if   (HS_KEY_DWORDS == 1)
#define HS_SHUFFLE_CAST_TO(v)         v
#define HS_SHUFFLE_CAST_FROM(v)       v
#elif (HS_KEY_DWORDS == 2)
#define HS_SHUFFLE_CAST_TO(v)         uint64BitsToDouble(v)
#define HS_SHUFFLE_CAST_FROM(v)       doubleBitsToUint64(v)
#endif

#define HS_SUBGROUP_SHUFFLE(v,i)      HS_SHUFFLE_CAST_FROM(subgroupShuffle(HS_SHUFFLE_CAST_TO(v),i))
#define HS_SUBGROUP_SHUFFLE_XOR(v,m)  HS_SHUFFLE_CAST_FROM(subgroupShuffleXor(HS_SHUFFLE_CAST_TO(v),m))
#define HS_SUBGROUP_SHUFFLE_UP(v,d)   HS_SHUFFLE_CAST_FROM(subgroupShuffleUp(HS_SHUFFLE_CAST_TO(v),d))
#define HS_SUBGROUP_SHUFFLE_DOWN(v,d) HS_SHUFFLE_CAST_FROM(subgroupShuffleDown(HS_SHUFFLE_CAST_TO(v),d))

//
// SLAB GLOBAL
//

//
// Note that subgroup sizes in Vulkan 1.1 aren't necessarily known in
// advance -- at least on Intel GEN.  When we have control over which
// subgroup size is selected at shader compile time then some of the
// address calculation macros will be written in a less subtle manner.
//

#define HS_SLAB_GLOBAL_BASE()                                                                   \
  const uint gmem_base = (gl_GlobalInvocationID.x & ~(HS_SLAB_THREADS-1)) * HS_SLAB_HEIGHT

#define HS_SLAB_GLOBAL_OFFSET()                                                 \
  const uint gmem_offset = (gl_LocalInvocationID.x & (HS_SLAB_THREADS-1))

//
//
//

#define HS_SLAB_GLOBAL_IDX()                    \
  HS_SLAB_GLOBAL_BASE();                        \
  HS_SLAB_GLOBAL_OFFSET();                      \
  const uint gmem_idx = gmem_base + gmem_offset

#define HS_SLAB_GLOBAL_IDX_IN()                 \
  HS_SLAB_GLOBAL_IDX();                         \
  const uint gmem_in_idx = kv_offset_in + gmem_idx

#define HS_SLAB_GLOBAL_IDX_OUT()                        \
  HS_SLAB_GLOBAL_IDX();                                 \
  const uint gmem_out_idx = kv_offset_out + gmem_idx

#define HS_SLAB_GLOBAL_IDX_IN_OUT()                        \
  HS_SLAB_GLOBAL_IDX();                                    \
  const uint gmem_in_idx  = kv_offset_in  + gmem_idx;      \
  const uint gmem_out_idx = kv_offset_out + gmem_idx

//
//
//

#define HS_SLAB_GLOBAL_LOAD_IN(_row_idx)                        \
  HS_KV_IN_LOAD(gmem_in_idx + HS_SLAB_THREADS * _row_idx)

#define HS_SLAB_GLOBAL_LOAD_OUT(_row_idx)                       \
  HS_KV_OUT_LOAD(gmem_out_idx + HS_SLAB_THREADS * _row_idx)

#define HS_SLAB_GLOBAL_STORE_OUT(_row_idx,_kv)                          \
  HS_KV_OUT_STORE((gmem_out_idx + HS_SLAB_THREADS * _row_idx),_kv)

//
// SLAB LOCAL
//

#define HS_SLAB_LOCAL_L(offset)                 \
  smem.m[smem_l_idx + (offset)]

#define HS_SLAB_LOCAL_R(offset)                 \
  smem.m[smem_r_idx + (offset)]

//
// SLAB LOCAL VERTICAL LOADS
//

#define HS_BX_LOCAL_V(offset)                   \
  smem.m[gl_LocalInvocationID.x + (offset)]

//
// BLOCK SORT MERGE HORIZONTAL
//

#define HS_BS_MERGE_H_PREAMBLE(slab_count)                      \
  const uint smem_l_idx =                                       \
    HS_SUBGROUP_ID() * (HS_SLAB_THREADS * slab_count) +         \
    HS_SUBGROUP_LANE_ID();                                      \
  const uint smem_r_idx =                                       \
    (HS_SUBGROUP_ID() ^ 1) * (HS_SLAB_THREADS * slab_count) +   \
    (HS_SUBGROUP_LANE_ID() ^ (HS_SLAB_THREADS - 1))

//
// BLOCK CLEAN MERGE HORIZONTAL
//

#define HS_BC_MERGE_H_PREAMBLE(slab_count)                              \
  const uint gmem_l_idx =                                               \
    (gl_GlobalInvocationID.x & ~(HS_SLAB_THREADS * slab_count -1))      \
    * HS_SLAB_HEIGHT + gl_LocalInvocationID.x;                          \
  const uint smem_l_idx =                                               \
    HS_SUBGROUP_ID() * (HS_SLAB_THREADS * slab_count) +                 \
    HS_SUBGROUP_LANE_ID()

#define HS_BC_GLOBAL_LOAD_L(slab_idx)                           \
  HS_KV_OUT_LOAD(gmem_l_idx + (HS_SLAB_THREADS * slab_idx))

//
// SLAB FLIP AND HALF PREAMBLES
//

#if 0

#define HS_SLAB_FLIP_PREAMBLE(mask)                                     \
  const uint flip_lane_idx = HS_SUBGROUP_LANE_ID() ^ mask;              \
  const bool t_lt          = HS_SUBGROUP_LANE_ID() < flip_lane_idx

#define HS_SLAB_HALF_PREAMBLE(mask)                                     \
  const uint half_lane_idx = HS_SUBGROUP_LANE_ID() ^ mask;              \
  const bool t_lt          = HS_SUBGROUP_LANE_ID() < half_lane_idx

#else

#define HS_SLAB_FLIP_PREAMBLE(mask)                                                     \
  const uint flip_lane_mask = mask;                                                     \
  const bool t_lt           = gl_LocalInvocationID.x < (gl_LocalInvocationID.x ^ mask)

#define HS_SLAB_HALF_PREAMBLE(mask)                                                     \
  const uint half_lane_mask = mask;                                                     \
  const bool t_lt           = gl_LocalInvocationID.x < (gl_LocalInvocationID.x ^ mask)

#endif

//
// Inter-lane compare exchange
//

// best on 32-bit keys
#define HS_CMP_XCHG_V0(a,b)                     \
  {                                             \
    const HS_KEY_TYPE t = min(a,b);             \
    b = max(a,b);                               \
    a = t;                                      \
  }

// good on Intel GEN 32-bit keys
#define HS_CMP_XCHG_V1(a,b)                     \
  {                                             \
    const HS_KEY_TYPE tmp = a;                  \
    a  = (a < b) ? a : b;                       \
    b ^= a ^ tmp;                               \
  }

// best on 64-bit keys
#define HS_CMP_XCHG_V2(a,b)                     \
  if (a >= b) {                                 \
    const HS_KEY_TYPE t = a;                    \
    a = b;                                      \
    b = t;                                      \
  }

// ok
#define HS_CMP_XCHG_V3(a,b)                     \
  {                                             \
    const bool        ge = a >= b;              \
    const HS_KEY_TYPE t  = a;                   \
    a = ge ? b : a;                             \
    b = ge ? t : b;                             \
  }

//
// The flip/half comparisons rely on a "conditional min/max":
//
//  - if the flag is false, return min(a,b)
//  - otherwise, return max(a,b)
//
// What's a little surprising is that sequence (1) is faster than (2)
// for 32-bit keys.
//
// I suspect either a code generation problem or that the sequence
// maps well to the GEN instruction set.
//
// We mostly care about 64-bit keys and unsurprisingly sequence (2) is
// fastest for this wider type.
//

#define HS_LOGICAL_XOR()  !=

// this is what you would normally use
#define HS_COND_MIN_MAX_V0(lt,a,b) ((a <= b) HS_LOGICAL_XOR() lt) ? b : a

// this seems to be faster for 32-bit keys on Intel GEN
#define HS_COND_MIN_MAX_V1(lt,a,b) (lt ? b : a) ^ ((a ^ b) & HS_LTE_TO_MASK(a,b))

//
// Conditional inter-subgroup flip/half compare exchange
//

#if 0

#define HS_CMP_FLIP(i,a,b)                                              \
  {                                                                     \
    const HS_KEY_TYPE ta = HS_SUBGROUP_SHUFFLE(a,flip_lane_idx);        \
    const HS_KEY_TYPE tb = HS_SUBGROUP_SHUFFLE(b,flip_lane_idx);        \
    a = HS_COND_MIN_MAX(t_lt,a,tb);                                     \
    b = HS_COND_MIN_MAX(t_lt,b,ta);                                     \
  }

#define HS_CMP_HALF(i,a)                                                \
  {                                                                     \
    const HS_KEY_TYPE ta = HS_SUBGROUP_SHUFFLE(a,half_lane_idx);        \
    a = HS_COND_MIN_MAX(t_lt,a,ta);                                     \
  }

#else

#define HS_CMP_FLIP(i,a,b)                                              \
  {                                                                     \
    const HS_KEY_TYPE ta = HS_SUBGROUP_SHUFFLE_XOR(a,flip_lane_mask);   \
    const HS_KEY_TYPE tb = HS_SUBGROUP_SHUFFLE_XOR(b,flip_lane_mask);   \
    a = HS_COND_MIN_MAX(t_lt,a,tb);                                     \
    b = HS_COND_MIN_MAX(t_lt,b,ta);                                     \
  }

#define HS_CMP_HALF(i,a)                                                \
  {                                                                     \
    const HS_KEY_TYPE ta = HS_SUBGROUP_SHUFFLE_XOR(a,half_lane_mask);   \
    a = HS_COND_MIN_MAX(t_lt,a,ta);                                     \
  }

#endif

//
// The device's comparison operator might return what we actually
// want.  For example, it appears GEN 'cmp' returns {true:-1,false:0}.
//

#define HS_CMP_IS_ZERO_ONE

#ifdef HS_CMP_IS_ZERO_ONE
// OpenCL requires a {true: +1, false: 0} scalar result
// (a < b) -> { +1, 0 } -> NEGATE -> { 0, 0xFFFFFFFF }
#define HS_LTE_TO_MASK(a,b) (HS_KEY_TYPE)(-(a <= b))
#define HS_CMP_TO_MASK(a)   (HS_KEY_TYPE)(-a)
#else
// However, OpenCL requires { -1, 0 } for vectors
// (a < b) -> { 0xFFFFFFFF, 0 }
#define HS_LTE_TO_MASK(a,b) (a <= b) // FIXME for uint64
#define HS_CMP_TO_MASK(a)   (a)
#endif

//
// The "flip-merge" and "half-merge" preambles are very similar
//
// For now, we're only using the .y dimension for the span idx
//

#define HS_HM_PREAMBLE(half_span)                                       \
  const uint span_idx    = gl_WorkGroupID.y;                            \
  const uint span_stride = gl_NumWorkGroups.x * gl_WorkGroupSize.x;     \
  const uint span_size   = span_stride * half_span * 2;                 \
  const uint span_base   = span_idx * span_size;                        \
  const uint span_off    = gl_GlobalInvocationID.x;                     \
  const uint span_l      = span_base + span_off

#define HS_FM_PREAMBLE(half_span)                                                       \
  HS_HM_PREAMBLE(half_span);                                                            \
  const uint span_r      = span_base + span_stride * (half_span + 1) - span_off - 1

//
//
//

#define HS_XM_GLOBAL_L(stride_idx)              \
  (span_l + span_stride * stride_idx)

#define HS_XM_GLOBAL_LOAD_L(stride_idx)         \
  HS_KV_OUT_LOAD(HS_XM_GLOBAL_L(stride_idx))

#define HS_XM_GLOBAL_STORE_L(stride_idx,reg)            \
  HS_KV_OUT_STORE(HS_XM_GLOBAL_L(stride_idx),reg)

#define HS_FM_GLOBAL_R(stride_idx)              \
  (span_r + span_stride * stride_idx)

#define HS_FM_GLOBAL_LOAD_R(stride_idx)         \
  HS_KV_OUT_LOAD(HS_FM_GLOBAL_R(stride_idx))

#define HS_FM_GLOBAL_STORE_R(stride_idx,reg)            \
  HS_KV_OUT_STORE(HS_FM_GLOBAL_R(stride_idx),reg)

//
//
//

#define HS_FILL_IN_BODY()                                               \
  HS_SLAB_GLOBAL_BASE();                                                \
  HS_SLAB_GLOBAL_OFFSET();                                              \
  uint kv_count_base = kv_count & ~(HS_SLAB_THREADS-1);                 \
  uint gmem_in_idx   = kv_offset_in + gmem_offset;                      \
  if (gmem_base >= kv_count_base)                                       \
    {                                                                   \
      gmem_in_idx += gmem_base;                                         \
    }                                                                   \
  else                                                                  \
    {                                                                   \
      gmem_in_idx += kv_count_base;                                     \
      if (gmem_in_idx >= (kv_offset_in + kv_count)) {                   \
        HS_KV_IN_STORE(gmem_in_idx,HS_KEY_VAL_MAX);                     \
        gmem_in_idx += HS_SLAB_THREADS;                                 \
      }                                                                 \
    }                                                                   \
  const uint gmem_in_max = gmem_base + kv_offset_in + HS_SLAB_KEYS;     \
  while (gmem_in_idx < gmem_in_max)                                     \
    {                                                                   \
      HS_KV_IN_STORE(gmem_in_idx,HS_KEY_VAL_MAX);                       \
      gmem_in_idx += HS_SLAB_THREADS;                                   \
    }

#define HS_FILL_OUT_BODY()                                                 \
  HS_SLAB_GLOBAL_IDX();                                                    \
  uint       gmem_out_idx = kv_offset_out + gmem_idx;                      \
  const uint gmem_out_max = gmem_base + kv_offset_out + HS_SLAB_KEYS;      \
  while (gmem_out_idx < gmem_out_max)                                      \
    {                                                                      \
      HS_KV_OUT_STORE(gmem_out_idx,HS_KEY_VAL_MAX);                        \
      gmem_out_idx += HS_SLAB_THREADS;                                     \
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

#define HS_TRANSPOSE_REG(prefix,row)   prefix##row
#define HS_TRANSPOSE_DECL(prefix,row)  const HS_KEY_TYPE HS_TRANSPOSE_REG(prefix,row)
#define HS_TRANSPOSE_PRED(level)       is_lo_##level

#define HS_TRANSPOSE_TMP_REG(prefix_curr,row_ll,row_ur) \
  prefix_curr##row_ll##_##row_ur

#define HS_TRANSPOSE_TMP_DECL(prefix_curr,row_ll,row_ur)                \
  const HS_KEY_TYPE HS_TRANSPOSE_TMP_REG(prefix_curr,row_ll,row_ur)

#define HS_TRANSPOSE_STAGE(level)                       \
  const bool HS_TRANSPOSE_PRED(level) =                 \
    (HS_SUBGROUP_LANE_ID() & (1 << (level-1))) == 0;

#define HS_TRANSPOSE_BLEND(prefix_prev,prefix_curr,level,row_ll,row_ur) \
  HS_TRANSPOSE_TMP_DECL(prefix_curr,row_ll,row_ur) =                    \
    HS_SUBGROUP_SHUFFLE_XOR(HS_TRANSPOSE_PRED(level) ?                  \
                            HS_TRANSPOSE_REG(prefix_prev,row_ll) :      \
                            HS_TRANSPOSE_REG(prefix_prev,row_ur),       \
                            1<<(level-1));                              \
                                                                        \
  HS_TRANSPOSE_DECL(prefix_curr,row_ll) =                               \
    HS_TRANSPOSE_PRED(level)                  ?                         \
    HS_TRANSPOSE_TMP_REG(prefix_curr,row_ll,row_ur) :                   \
    HS_TRANSPOSE_REG(prefix_prev,row_ll);                               \
                                                                        \
  HS_TRANSPOSE_DECL(prefix_curr,row_ur) =                               \
    HS_TRANSPOSE_PRED(level)                  ?                         \
    HS_TRANSPOSE_REG(prefix_prev,row_ur)      :                         \
    HS_TRANSPOSE_TMP_REG(prefix_curr,row_ll,row_ur);

#define HS_TRANSPOSE_REMAP(prefix,row_from,row_to)                      \
  HS_KV_OUT_STORE(gmem_out_idx + ((row_to-1) << HS_SLAB_WIDTH_LOG2),    \
                  HS_TRANSPOSE_REG(prefix,row_from));

//
//
//

#endif

//
//
//
