// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "hs_config.h"
#include "spn_config.h"

//
//
//

#include "spn_target.h"

//
//
//

#include "vk_target.h"

//
//
//

#include <vulkan/vulkan_core.h>

//
// clang-format off
//

static struct spn_vk_target const target =
{
  .config = {

    //
    //
    //
    .queueing                             = SPN_VK_TARGET_QUEUEING_SIMPLE,

    .extensions.named = {
      .KHR_shader_float16_int8            = 1,
    },

    .features.named = {

    },

    .structures.named = {
      .ShaderFloat16Int8FeaturesKHR = {
        .shaderFloat16                    = 1,
      },
    },

    //
    //
    //
    .allocator = {
      .host = {
        .perm = {
          .alignment        = 16,      // 16 byte alignment
        }
      },
      .device = {
        .drw = {
          .properties       = (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
          .usage            = (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT   |
                               VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT  |
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT     |
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        },
        .hw_dr = {
          .properties       = (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT  |
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT  |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
          .usage            = (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT   |
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
        },
        .hrw_dr = {
          .properties       = (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT  |
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT  |
                               VK_MEMORY_PROPERTY_HOST_CACHED_BIT),
          .usage            = (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
        },
        .hr_dw = {
          .properties       = (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT  |
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT  |
                               VK_MEMORY_PROPERTY_HOST_CACHED_BIT),
          .usage            = (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT   |
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        },
        .temp = {
          .subbufs          = 256,      // 256 subbufs
          .size             = 32 << 20, // 32 MBytes
        }
      }
    },

    .tile = {
      .width_log2           = SPN_DEVICE_TILE_WIDTH_LOG2,
      .height_log2          = SPN_DEVICE_TILE_HEIGHT_LOG2
    },

    .block_pool = {
      .block_dwords_log2    = SPN_DEVICE_BLOCK_POOL_BLOCK_DWORDS_LOG2,
      .subblock_dwords_log2 = SPN_DEVICE_BLOCK_POOL_SUBBLOCK_DWORDS_LOG2,
      .ids_per_invocation   = SPN_DEVICE_BLOCK_POOL_INIT_BP_IDS_PER_INVOCATION
    },

    .path_builder = {
      .size = {
        .dispatches         = 32,
        .ring               = 16384,
        .eager              = 4096
      }
    },

    .raster_builder = {
      .size = {
        .dispatches         = 32,
        .ring               = 8192,
        .eager              = 1024,
        .cohort             = SPN_DEVICE_RASTERIZE_COHORT_SIZE,
        .cmds               = 1 << 18,
        .ttrks              = 1 << 20
      },
      .fills_scan = {
        .rows               = SPN_DEVICE_FILLS_SCAN_ROWS
      }
    },

    .composition = {
      .size = {
        .dispatches         = 32,
        .ring               = 8192,
        .eager              = 1024,
        .ttcks              = 1 << 20,
        .rasters            = 1 << 17
      }
    },

    .reclaim = {
      .size = {
        .dispatches         = 32,
        .paths              = 16384,
        .rasters            = 16384,
        .eager              = 1024
      }
    },

    //
    // capture target-specific number of sets and extent sizes
    //
#define SPN_DS_WAG_COUNT  64

    .ds = {
      .status = {
        .sets = 1
      },
      .block_pool = {
        .sets = 1
      },
      .paths_copy = {
        .sets = SPN_DS_WAG_COUNT
      },
      .rasterize = {
        .sets = SPN_DS_WAG_COUNT
      },
      .ttrks = {
        .sets = SPN_DS_WAG_COUNT
      },
      .raster_ids = {
        .sets = SPN_DS_WAG_COUNT
      },
      .ttcks = {
        .sets = SPN_DS_WAG_COUNT
      },
      .place = {
        .sets = SPN_DS_WAG_COUNT
      },
      .styling = {
        .sets = SPN_DS_WAG_COUNT
      },
      .surface = {
        .sets = SPN_DS_WAG_COUNT
      },
      .reclaim = {
        .sets = SPN_DS_WAG_COUNT
      }
    },

    //
    // Initialize pipeline-specific parameters
    //
    .p = {
#include "target_pipelines.inl"
    }
  },

  //
  // FILL IN REST OF SPINEL CONFIG STRUCT FROM OPENCL
  //
  .modules = {
#include "spn_modules.inl"

#ifdef SPN_DUMP
    0
#endif
  }
};

//
// clang-format on
//

struct spn_vk_target const * const SPN_TARGET_NAME = &target;

#include "target_modules_dump.inl"

//
//
//
