// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include <vulkan/vulkan_core.h>

//
//
//

#include "config.h"
#include "name.h"

//
// clang-format off
//

#define SPN_TARGET_EXTENT_PDRW         (SPN_TARGET_ALLOC_PERM_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
#define SPN_TARGET_EXTENT_TDRW         (SPN_TARGET_ALLOC_TEMP_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
#define SPN_TARGET_EXTENT_PHW1G_TDR1S  (SPN_TARGET_ALLOC_PERM_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
#define SPN_TARGET_EXTENT_PHW1G_TDRNS  (SPN_TARGET_ALLOC_PERM_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
#define SPN_TARGET_EXTENT_PHWN_PDRN    (SPN_TARGET_ALLOC_PERM_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) // bad
#define SPN_TARGET_EXTENT_IMAGE        0

//
//
//

struct spn_target_image const SPN_TARGET_IMAGE_NAME =
{
  .config = {
    .allocator = {
      .host = {
        .perm = {
          .alignment        = 16,      // 16 byte alignment
        }
      }
    },

    .fence_pool = {
      .size                 = 2,       // ~16-256 "in-flight" submits
    },

    .subgroup_size_log2     = SPN_DEVICE_SUBGROUP_SIZE_LOG2,

    .tile = {
      .width_log2           = SPN_DEVICE_TILE_WIDTH_LOG2,
      .height_log2          = SPN_DEVICE_TILE_HEIGHT_LOG2
    },

    .block_pool = {
      .block_dwords_log2    = SPN_DEVICE_BLOCK_POOL_BLOCK_DWORDS_LOG2,
      .subblock_dwords_log2 = SPN_DEVICE_BLOCK_POOL_SUBBLOCK_DWORDS_LOG2,
      .ids_per_workgroup    = SPN_DEVICE_BLOCK_POOL_INIT_BP_IDS_PER_WORKGROUP
    },

    .path_builder = {
      .ring_size            = 16834,
      .eager_size           = 4096
    },

    .raster_builder = {
      .vk = {
        .rings = {
          .h                = 0,   // FIXME -- replace with extent type
          .d                = 0
        }
      },
      .size = {
        .ring               = 8192,
        .eager              = 1024,
        .cohort             = SPN_DEVICE_RASTERS_ALLOC_METAS_SIZE, // FIXME -- change name
        .cmds               = 1 << 18,
        .ttrks              = 1 << 20
      }
    },

    .styling = {
      .vk = {
        .h                  = 0,   // FIXME -- replace with extent type
        .d                  = 0
      }
    },

    .composition = {
      .vk = {
        .rings = {
          .h                = 0,   // FIXME -- replace with extent type
          .d                = 1
        }
      },
      .size = {
        .ring               = 8192,
        .eager              = 1024,
        .cmds               = 1 << 18,
        .ttcks              = 1 << 20,
        .rasters            = 1 << 17
      }
    },

    //
    // capture target-specific number of sets and extent sizes
    //
    .ds = {
      .block_pool = {
        .sets = 1
      },
      .paths_copy = {
        .sets = 1
      },
      .rasterize = {
        .sets = 1
      },
      .rasterize_post = {
        .sets = 1
      },
      .ttcks = {
        .sets = 1
      },
      .place = {
        .sets = 1
      },
      .styling = {
        .sets = 1
      },
      .surface = {
        .sets = 1
      }
    },


    //
    // capture target-specific extent types
    //
#undef  SPN_TARGET_DESC_TYPE_STORAGE_BUFFER
#define SPN_TARGET_DESC_TYPE_STORAGE_BUFFER(_ds_id,_d_idx,_d_ext,_d_id) ._d_id = _d_ext,

#undef  SPN_TARGET_DESC_TYPE_STORAGE_IMAGE
#define SPN_TARGET_DESC_TYPE_STORAGE_IMAGE(_ds_id,_d_idx,_d_ext,_d_id)  ._d_id = _d_ext,

#undef  SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx,_ds_id,_ds)      \
    .ds_extents._ds_id.props = {                        \
      _ds                                               \
    },

    SPN_TARGET_DS_EXPAND()

    //
    // capture target-specific pipeline push constant sizes
    //
    .p = {
      .push_sizes = {
#include "targets/push.inl"
      }
    }
  },

  //
  // FILL IN REST OF SPINEL CONFIG STRUCT FROM OPENCL
  //
  .modules = {
#if defined( _MSC_VER ) && !defined( __clang__ )
#include "targets/modules.inl"
#else
#include "block_pool_init.len.xxd"
    ,
#include "block_pool_init.spv.xxd"
    ,
#include "fills_dispatch.len.xxd"
    ,
#include "fills_dispatch.spv.xxd"
    ,
#include "fills_expand.len.xxd"
    ,
#include "fills_expand.spv.xxd"
    ,
#include "fills_scan.len.xxd"
    ,
#include "fills_scan.spv.xxd"
    ,
#include "paths_alloc.len.xxd"
    ,
#include "paths_alloc.spv.xxd"
    ,
#include "paths_copy.len.xxd"
    ,
#include "paths_copy.spv.xxd"
    ,
#include "paths_reclaim.len.xxd"
    ,
#include "paths_reclaim.spv.xxd"
    ,
#include "place.len.xxd"
    ,
#include "place.spv.xxd"
    ,
#include "rasterize_line.len.xxd"
    ,
#include "rasterize_line.spv.xxd"
    ,
#include "rasterize_quad.len.xxd"
    ,
#include "rasterize_quad.spv.xxd"
    ,
#include "rasterize_cubic.len.xxd"
    ,
#include "rasterize_cubic.spv.xxd"
    ,
#include "rasterize_rat_quad.len.xxd"
    ,
#include "rasterize_rat_quad.spv.xxd"
    ,
#include "rasterize_rat_cubic.len.xxd"
    ,
#include "rasterize_rat_cubic.spv.xxd"
    ,
#include "rasters_alloc.len.xxd"
    ,
#include "rasters_alloc.spv.xxd"
    ,
#include "rasters_prefix.len.xxd"
    ,
#include "rasters_prefix.spv.xxd"
    ,
#include "rasters_reclaim.len.xxd"
    ,
#include "rasters_reclaim.spv.xxd"
    ,
#include "render.len.xxd"
    ,
#include "render.spv.xxd"
    ,
#include "segment_ttck.len.xxd"
    ,
#include "segment_ttck.spv.xxd"
    ,
#include "segment_ttrk.len.xxd"
    ,
#include "segment_ttrk.spv.xxd"
    ,
#endif

#ifdef SPN_TARGET_IMAGE_DUMP
    0,0,0,0
#endif
  }
};

//
// clang-format on
//

#include "targets/dump.inl"

//
//
//
