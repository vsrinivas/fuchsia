// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#if defined(_MSC_VER)

#define _CRT_SECURE_NO_WARNINGS  // squelch fopen()

#endif

//
//
//

#include "pipeline_cache.h"

#include <stdio.h>
#include <stdlib.h>

#include "assert.h"

//
//
//

#ifdef VK_PIPELINE_CACHE_DEBUG_ENABLE
#define VK_PIPELINE_CACHE_DEBUG(...) fprintf(stderr, ##__VA_ARGS__)
#else
#define VK_PIPELINE_CACHE_DEBUG(...)
#endif

//
//
//

VkResult
vk_pipeline_cache_create(VkDevice                      device,
                         VkAllocationCallbacks const * allocator,
                         char const * const            name,
                         VkPipelineCache *             pipeline_cache)
{
  VkPipelineCacheCreateInfo pipeline_cache_info = {

    .sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
    .pNext           = NULL,
    .flags           = 0,
    .initialDataSize = 0,
    .pInitialData    = NULL
  };

  FILE * f    = fopen(name, "rb");
  void * data = NULL;

  if (f != NULL)
    {
      VK_PIPELINE_CACHE_DEBUG("%s  : opened file \"%s\"\n", __func__, name);

      if (fseek(f, 0L, SEEK_END) == 0)
        {
          size_t const data_size = ftell(f);

          if (data_size > 0)
            {
              fseek(f, 0L, SEEK_SET);

              data = malloc(data_size);

              if (data != NULL)
                {
                  fread(data, data_size, 1, f);

                  pipeline_cache_info.initialDataSize = data_size;
                  pipeline_cache_info.pInitialData    = data;
                }
            }

          VK_PIPELINE_CACHE_DEBUG("%s  : size of file \"%s\" is %zu\n", __func__, name, data_size);
        }
      else
        {
          VK_PIPELINE_CACHE_DEBUG("%s  : couldn't fseek() file \"%s\"\n", __func__, name);
        }

      fclose(f);
    }
  else
    {
      VK_PIPELINE_CACHE_DEBUG("%s  : couldn't open file \"%s\"\n", __func__, name);
    }

  VkResult vk_res = vkCreatePipelineCache(device, &pipeline_cache_info, allocator, pipeline_cache);

  free(data);  // Doesn't matter if data is NULL

  return vk_res;
}

//
//
//

VkResult
vk_pipeline_cache_destroy(VkDevice                      device,
                          VkAllocationCallbacks const * allocator,
                          char const * const            name,
                          VkPipelineCache               pipeline_cache)
{
  size_t   data_size;
  VkResult vk_res = vkGetPipelineCacheData(device, pipeline_cache, &data_size, NULL);

  if (vk_res != VK_SUCCESS)
    {
      return vk_res;
    }

  if (data_size > 0)
    {
      void * data = malloc(data_size);

      if (data != NULL)
        {
          vk_res = vkGetPipelineCacheData(device, pipeline_cache, &data_size, data);

          if (vk_res == VK_SUCCESS)
            {
              FILE * f = fopen(name, "wb");

              if (f != NULL)
                {
                  VK_PIPELINE_CACHE_DEBUG("%s : opened file \"%s\"\n", __func__, name);

                  fwrite(data, data_size, 1, f);
                  fclose(f);
                }
              else
                {
                  VK_PIPELINE_CACHE_DEBUG("%s : couldn't open file \"%s\"\n", __func__, name);
                }
            }
        }

      free(data);  // Doesn't matter if data is NULL
    }

  vkDestroyPipelineCache(device, pipeline_cache, allocator);

  return vk_res;
}

//
//
//
