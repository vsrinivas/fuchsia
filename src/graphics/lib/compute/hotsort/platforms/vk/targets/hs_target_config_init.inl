// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

.is_in_place     = HS_IS_IN_PLACE,

.slab = {
   .threads_log2 = HS_SLAB_THREADS_LOG2,
   .width_log2   = HS_SLAB_WIDTH_LOG2,
   .height       = HS_SLAB_HEIGHT
 },

.dwords = {
   .key          = HS_KEY_DWORDS,
   .val          = HS_VAL_DWORDS
 },

.block = {
   .slabs        = HS_BS_SLABS
 },

.merge = {
   .fm = {
     .scale_min  = HS_FM_SCALE_MIN,
     .scale_max  = HS_FM_SCALE_MAX
   },
   .hm = {
     .scale_min  = HS_HM_SCALE_MIN,
     .scale_max  = HS_HM_SCALE_MAX,
   }
 }

//
//
//
