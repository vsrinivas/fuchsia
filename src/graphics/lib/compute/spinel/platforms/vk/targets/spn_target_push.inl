// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Record the push sizes
//

//
// clang-format off
//

#undef  SPN_VK_TARGET_PUSH_UINT
#undef  SPN_VK_TARGET_PUSH_UVEC4
#undef  SPN_VK_TARGET_PUSH_IVEC4
#undef  SPN_VK_TARGET_PUSH_UINT_FARRAY
#undef  SPN_VK_TARGET_PUSH_UINT_VARRAY

#define SPN_VK_TARGET_PUSH_UINT(name)            + sizeof(SPN_TYPE_UINT)
#define SPN_VK_TARGET_PUSH_UVEC4(name)           + sizeof(SPN_TYPE_UVEC4)
#define SPN_VK_TARGET_PUSH_IVEC4(name)           + sizeof(SPN_TYPE_IVEC4)
#define SPN_VK_TARGET_PUSH_UINT_FARRAY(name,len) + sizeof(SPN_TYPE_UINT) * len
#define SPN_VK_TARGET_PUSH_UINT_VARRAY(name,len) + sizeof(SPN_TYPE_UINT) * len

#undef  SPN_VK_TARGET_VK_DS
#define SPN_VK_TARGET_VK_DS(_p_id,_ds_idx,_ds_id)

#undef  SPN_VK_TARGET_VK_PUSH
#define SPN_VK_TARGET_VK_PUSH(_p_id,_p_pc)  ._p_id = (0 _p_pc),

.named = {
#undef  SPN_VK_TARGET_P_EXPAND_X
#define SPN_VK_TARGET_P_EXPAND_X(_p_idx,_p_id,_p_descs)      \
   _p_descs

   SPN_VK_TARGET_P_EXPAND()
},

//
// clang-format on
//
