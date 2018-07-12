// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/escher/util/check_vulkan_support.h"

int main(int argc, const char** argv) {
  return escher::VulkanIsSupported() ? 0 : 1;
}
