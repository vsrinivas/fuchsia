// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>

#include "garnet/public/lib/escher/util/check_vulkan_support.h"

int main(int argc, const char** argv) {
  bool vulkan_is_supported = escher::VulkanIsSupported();
  printf("%c\n", vulkan_is_supported ? '1' : '0');
  // TODO(PT-71): Always have an exit status of 0 once the above print rolls
  // into topaz.
  return vulkan_is_supported ? 0 : 1;
}
