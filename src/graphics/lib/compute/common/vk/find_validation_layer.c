// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

//
//
//

#include "common/macros.h"
#include "common/vk/assert.h"
#include "find_validation_layer.h"

//
//
//

const char *
vk_find_validation_layer()
{
  uint32_t layer_count;
  vk(EnumerateInstanceLayerProperties(&layer_count, NULL));

  VkLayerProperties * layer_properties = malloc(layer_count * sizeof(VkLayerProperties));
  vk(EnumerateInstanceLayerProperties(&layer_count, layer_properties));

  bool found_khr_validation = false;

  for (uint32_t i = 0; i < layer_count; i++)
    {
      found_khr_validation = found_khr_validation || (strcmp(layer_properties[i].layerName,
                                                             "VK_LAYER_KHRONOS_validation") == 0);
    }

  free(layer_properties);

  return found_khr_validation ? "VK_LAYER_KHRONOS_validation" : NULL;
}
//
//
//
