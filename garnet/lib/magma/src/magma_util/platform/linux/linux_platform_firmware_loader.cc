// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_firmware_loader.h"

namespace magma {

std::unique_ptr<PlatformFirmwareLoader> PlatformFirmwareLoader::Create(void* device_handle) {
  return DRETP(nullptr, "Not implemented");
}

}  // namespace magma
