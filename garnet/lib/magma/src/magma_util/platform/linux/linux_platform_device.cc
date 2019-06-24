// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "linux_platform_device.h"

#include <sys/ioctl.h>
#include <sys/mman.h>

namespace {

// Generic DRM definitions; copied from drm.h
#define DRM_IOCTL_BASE 'd'
#define DRM_COMMAND_BASE 0x40
#define DRM_COMMAND_END 0xA0
#define DRM_IOWR(nr, type) _IOWR(DRM_IOCTL_BASE, nr, type)

} // namespace

namespace {

// Magma specific DRM definitions
struct magma_param {
    uint64_t key;
    uint64_t value;
};

#define DRM_MAGMA_GET_PARAM 0x20

#define DRM_IOCTL_MAGMA_GET_PARAM                                                                  \
    DRM_IOWR(DRM_COMMAND_BASE + DRM_MAGMA_GET_PARAM, struct magma_param)

} // namespace

namespace magma {

bool LinuxPlatformDevice::MagmaGetParam(MagmaGetParamKey key, uint64_t* value_out)
{
    struct magma_param param = {.key = static_cast<uint64_t>(key)};

    if (ioctl(fd_, DRM_IOCTL_MAGMA_GET_PARAM, &param) != 0)
        return false;

    *value_out = param.value;
    return true;
}

std::unique_ptr<PlatformMmio>
LinuxPlatformDevice::CpuMapMmio(unsigned int index, PlatformMmio::CachePolicy cache_policy)
{
    if (cache_policy != PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE)
        return DRETP(nullptr, "Unsupported cache policy");

    uint64_t length;
    if (!MagmaGetParam(MagmaGetParamKey::kRegisterSize, &length))
        return DRETP(nullptr, "MagmaGetParam failed");

    void* cpu_addr = mmap(nullptr, // desired addr
                          length, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd_,
                          0 // offset
    );

    if (cpu_addr == MAP_FAILED)
        return DRETP(nullptr, "mmap failed");

    return std::make_unique<LinuxPlatformMmio>(cpu_addr, length);
}

std::unique_ptr<PlatformDevice> PlatformDevice::Create(void* device_handle)
{
    if (!device_handle)
        return DRETP(nullptr, "device_handle is null, cannot create PlatformDevice");

    int fd_device = reinterpret_cast<intptr_t>(device_handle);

    return std::unique_ptr<PlatformDevice>(new LinuxPlatformDevice(fd_device));
}

} // namespace magma
