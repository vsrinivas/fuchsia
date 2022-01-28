// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include <vulkan/vulkan_core.h>

#include "config.h"
#include "core_c.h"
#include "target.h"

//
//
//

static struct spinel_target_header const header __attribute__((used)) =
{
  .magic = SPN_HEADER_MAGIC,

  .extensions.named = {
    .EXT_subgroup_size_control          = 1,
  },

  .features.named = {
    .shaderInt64                        = 1,
    .shaderFloat16                      = 1,
    .timelineSemaphore                  = 1,
    .bufferDeviceAddress                = 1,
  },

  .config = {

    .allocator = {
      .device = {
        .drw = {
          .properties       = (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
          .usage            = (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT   |
                               VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT  |
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT     |
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT     |
                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        },
        .hw_dr = {
          .properties       = (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT  |
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT  |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
          .usage            = (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT   |
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT     |
                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        },
        .hrw_dr = {
          .properties       = (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT  |
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT  |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                               VK_MEMORY_PROPERTY_HOST_CACHED_BIT),
          .usage            = (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT   |
                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        },
        .drw_shared = {
          .properties       = (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
          .usage            = (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT   |
                               VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT  |
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT     |
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT     |
                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        },
      }
    },

    .deps = {
      .semaphores = {
        .immediate = {
          .pool = {
            .size           = 1,
            .count          = 32,
          },
        },
        .delayed = {
          .size             = 32,
        }
      }
    },

    .tile = {
      .width_log2           = SPN_DEVICE_TILE_WIDTH_LOG2,
      .height_log2          = SPN_DEVICE_TILE_HEIGHT_LOG2
    },

    .pixel = {
      .width_log2           = SPN_TTS_SUBPIXEL_X_LOG2,
      .height_log2          = SPN_TTS_SUBPIXEL_Y_LOG2,
    },

    .block_pool = {
      .block_dwords_log2    = SPN_DEVICE_BLOCK_POOL_BLOCK_DWORDS_LOG2,
      .subblock_dwords_log2 = SPN_DEVICE_BLOCK_POOL_SUBBLOCK_DWORDS_LOG2,
      .ids_per_invocation   = SPN_DEVICE_BLOCK_POOL_INIT_BP_IDS_PER_INVOCATION
    },

    .path_builder = {
      .size = {
        .dispatches         = 4,
        .ring               = 4096,
        .eager              = 1024
      }
    },

    .raster_builder = {
      .size = {
        .dispatches         = 4,     // NOTE: every dispatch has dedicated allocations
        .ring               = 4096,
        .eager              = 1024,
        .cohort             = SPN_DEVICE_RASTERIZE_COHORT_SIZE,
        .cmds               = 1 << 18,
        .ttrks              = 1 << 20
      },
      .fill_scan = {
        .rows               = SPN_DEVICE_FILL_SCAN_ROWS
      }
    },

    .composition = {
      .size = {
        .dispatches         = 8,     // FIXME(allanmac): size correctly
        .ring               = 8192,  // These are commands
        .eager              = 1024,
        .ttcks              = 1 << 20,
        .rasters            = 1 << 17
      },
    },

    .swapchain = {
      .sharing_mode         = VK_SHARING_MODE_EXCLUSIVE,
      .texel_size           = 4,     // 32-bits per pixel for now
    },

    .reclaim = {
      .size = {
        .dispatches         = 8,     // FIXME(allanmac): size correctly
        .paths              = 8192,  // These are handles
        .rasters            = 8192,  // These are handles
        .eager              = 1024   // Must be less than handle rings
      }
    },

    .group_sizes = {
#include "target_init_group_sizes.h"
    },
  }
};

//
//
//
