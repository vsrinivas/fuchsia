// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lights-cli.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fbl/unique_fd.h>

const char* capabilities[3] = {"Brightness", "Rgb", "Simple"};

zx_status_t LightsCli::PrintValue(uint32_t idx) {
  auto result1 = client_.GetInfo(idx);
  if ((result1.status() != ZX_OK) || result1->result.has_invalid_tag()) {
    printf("Could not get info\n");
    return std::min(result1.status(), ZX_ERR_INTERNAL);
  }
  auto result2 = client_.GetCurrentBrightnessValue(idx);
  if ((result2.status() != ZX_OK) || result2->result.has_invalid_tag()) {
    printf("Could not get value\n");
    return std::min(result2.status(), ZX_ERR_INTERNAL);
  }

  printf("Value of %s: %f\n", result1->result.response().info.name.begin(),
         result2->result.response().value);
  return ZX_OK;
}

int LightsCli::SetValue(uint32_t idx, double value) {
  auto result = client_.SetBrightnessValue(idx, value);
  if ((result.status() != ZX_OK) || result->result.has_invalid_tag()) {
    printf("Could not set value\n");
    return std::min(result.status(), ZX_ERR_INTERNAL);
  }

  return ZX_OK;
}

zx_status_t LightsCli::Summary() {
  auto result1 = client_.GetNumLights();
  if (result1.status() != ZX_OK) {
    printf("Could not get count\n");
    return result1.status();
  }

  printf("Total %u lights\n", result1.value().count);
  for (uint32_t i = 0; i < result1.value().count; i++) {
    auto result2 = client_.GetInfo(i);
    if ((result2.status() != ZX_OK) || result2->result.has_invalid_tag()) {
      printf("Could not get capability for light number %u. Skipping.\n", i);
      continue;
    }

    zx_status_t status = ZX_OK;
    switch (result2->result.response().info.capability) {
      case fuchsia_hardware_light::wire::Capability::kBrightness:
        if ((status = PrintValue(i)) != ZX_OK) {
          printf("Print Value failed for light number %u.\n", i);
          continue;
        }
        break;
      case fuchsia_hardware_light::wire::Capability::kRgb:
        break;
      case fuchsia_hardware_light::wire::Capability::kSimple:
        break;
      default:
        printf("Unknown capability %u for light number %u.\n",
               result2->result.response().info.capability, i);
        continue;
    };

    printf("    Capabilities: %s\n",
           capabilities[static_cast<uint8_t>(result2->result.response().info.capability) - 1]);
  }

  return ZX_OK;
}
