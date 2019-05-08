// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>

#include "src/ui/lib/escher/util/check_vulkan_support.h"

int main(int argc, const char** argv) {
  printf("%c\n", escher::VulkanIsSupported() ? '1' : '0');
}
