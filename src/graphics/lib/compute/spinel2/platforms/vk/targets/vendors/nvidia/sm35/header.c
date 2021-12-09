// Copyright 2019 The Fuchsia Authors. All rights reserved.
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
    .NV_shader_subgroup_partitioned     = 1,
#ifndef NDEBUG
    .KHR_shader_non_semantic_info       = 1,
#endif
  },

  .features.named = {
    .shaderInt64                        = 1,
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
          .properties       = (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT  |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
          .usage            = (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT   |
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT     |
                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        },
        .hrw_dr = {
          .properties       = (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT  |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                               VK_MEMORY_PROPERTY_HOST_CACHED_BIT),
          .usage            = (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT   |
                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        },
        .hr_dw = {
          .properties       = (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT  |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                               VK_MEMORY_PROPERTY_HOST_CACHED_BIT),
          .usage            = (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT   |
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT     |
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
            .count          = 1
          },
        },
        .delayed = {
          .size             = 1
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
        .dispatches         = 32,
        .ring               = 16384, // blocks
        .eager              = 4096
      }
    },

    .raster_builder = {
      .no_staging           = 0,

      .size = {
        .dispatches         = 4, // NOTE: every dispatch allocates additional memory
        .ring               = 8192,
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
      .no_staging           = 0,

      .size = {
        .dispatches         = 32,
        .ring               = 8192,
        .eager              = 1024,
        .ttcks              = 1 << 20,
        .rasters            = 1 << 17
      },
    },

    .swapchain = {
      .sharing_mode         = VK_SHARING_MODE_EXCLUSIVE,
      .texel_size           = 4, // 32-bits per pixel for now
    },

    .reclaim = {
      .size = {
        .dispatches         = 32,
        .paths              = 16384,
        .rasters            = 16384,
        .eager              = 1024
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
