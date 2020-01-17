// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "demo_utils.h"

#include <stdio.h>
#include <string.h>

bool
parseDeviceOption(const char * option, uint32_t * vendor_id, uint32_t * device_id)
{
  *vendor_id = 0;
  *device_id = 0;

  if (option)
    {
      bool         format_ok;
      const char * sep = strchr(option, ':');
      if (!sep)
        {
          format_ok = sscanf(option, "%X", vendor_id) == 1;
        }
      else
        {
          format_ok = sscanf(option, "%X:%x", vendor_id, device_id) == 2;
        }
      if (!format_ok)
        {
          fprintf(stderr,
                  "Invalid --device argument, should be <HEXVENDOR> or <HEXVENDOR>:<HEXDEVICE>\n");
          return false;
        }
    }
  return true;
}

bool
parseWindowOption(const char * option,
                  uint32_t     default_width,
                  uint32_t     default_height,
                  uint32_t *   window_width,
                  uint32_t *   window_height)
{
  *window_width  = default_width;
  *window_height = default_height;

  if (option)
    {
      if (sscanf(option, "%ux%u", window_width, window_height) != 2)
        {
          fprintf(stderr, "Invalid --window argument, should be decimal <WIDTH>x<HEIGHT>.\n");
          return false;
        }
    }
  return true;
}

bool
parseFormatOption(const char * option, VkFormat * format)
{
  *format = VK_FORMAT_UNDEFINED;
  if (option)
    {
      // NOTE: Experience shows that, at least on NVidia, the _UNORM variant
      // support STORAGE_IMAGE but not the same formats with _SRGB!
      if (!strcmp(option, "bgra") || !strcmp(option, "BGRA"))
        *format = VK_FORMAT_B8G8R8A8_UNORM;
      else if (!strcmp(option, "rgba") || !strcmp(option, "RGBA"))
        *format = VK_FORMAT_R8G8B8A8_UNORM;
      else
        {
          fprintf(stderr, "Invalid --format value, should be one of: RGBA, BGRA\n");
          return false;
        }
    }
  return true;
}
