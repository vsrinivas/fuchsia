// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_FIRMWARE_LOADER_H_
#define GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_FIRMWARE_LOADER_H_

#include <memory>

#include "magma_util/dlog.h"
#include "magma_util/status.h"
#include "platform_buffer.h"

namespace magma {

class PlatformFirmwareLoader {
public:
    virtual ~PlatformFirmwareLoader() {}

    virtual Status LoadFirmware(const char* filename, std::unique_ptr<PlatformBuffer>* firmware_out,
                                uint64_t* size_out) = 0;

    static std::unique_ptr<PlatformFirmwareLoader> Create(void* device_handle);
};

} // namespace magma

#endif // GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_FIRMWARE_LOADER_H_
