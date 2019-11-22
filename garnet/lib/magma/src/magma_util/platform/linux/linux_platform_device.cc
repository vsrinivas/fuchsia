// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "linux_platform_device.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "platform_logger.h"

using __u64 = uint64_t;
using __u32 = uint32_t;

namespace {

// Generic DRM definitions; copied from drm.h
#define DRM_IOCTL_BASE 'd'
#define DRM_COMMAND_BASE 0x40
#define DRM_COMMAND_END 0xA0
#define DRM_IOWR(nr, type) _IOWR(DRM_IOCTL_BASE, nr, type)
#define DRM_IO(nr) _IO(DRM_IOCTL_BASE, nr)

}  // namespace

namespace {

// Copied from udmabuf.h
struct udmabuf_create {
  __u32 memfd;
  __u32 flags;
  __u64 offset;
  __u64 size;
};

#define UDMABUF_CREATE _IOW('u', 0x42, struct udmabuf_create)

}  // namespace

namespace {

struct magma_param {
  __u64 key;   /* in, MSM_PARAM_x */
  __u64 value; /* out (get_param) or in (set_param) */
};

struct magma_map_page_range_bus {
  /* IN */
  int dma_buf_fd;
  __u64 start_page_index;
  __u64 page_count;
  /* IN/OUT */
  __u64 token;
  __u64* bus_addr;
};

struct magma_map_gpu {
  /* IN */
  int map;
  __u64 gpu_addr;
  __u64 token;
};

#define DRM_MAGMA_GET_PARAM 0x20
#define DRM_MAGMA_MAP_PAGE_RANGE_BUS 0x21
#define DRM_MAGMA_MAP_GPU 0x22
#define DRM_MAGMA_RESET_GMU 0x23

#define DRM_IOCTL_MAGMA_GET_PARAM \
  DRM_IOWR(DRM_COMMAND_BASE + DRM_MAGMA_GET_PARAM, struct magma_param)
#define DRM_IOCTL_MAGMA_MAP_PAGE_RANGE_BUS \
  DRM_IOWR(DRM_COMMAND_BASE + DRM_MAGMA_MAP_PAGE_RANGE_BUS, struct magma_map_page_range_bus)
#define DRM_IOCTL_MAGMA_MAP_GPU DRM_IOWR(DRM_COMMAND_BASE + DRM_MAGMA_MAP_GPU, struct magma_map_gpu)
#define DRM_IOCTL_MAGMA_RESET_GMU DRM_IO(DRM_COMMAND_BASE + DRM_MAGMA_RESET_GMU)

}  // namespace

namespace magma {

std::unique_ptr<PlatformHandle> LinuxPlatformDevice::GetBusTransactionInitiator() const {
  int fd = dup(handle_.get());
  if (fd < 0)
    return DRETP(nullptr, "dup failed: %s", strerror(errno));

  return std::make_unique<LinuxPlatformHandle>(fd);
}

std::unique_ptr<PlatformHandle> LinuxPlatformDevice::GetIommuConnector() const {
  int fd = dup(handle_.get());
  if (fd < 0)
    return DRETP(nullptr, "dup failed: %s", strerror(errno));

  return std::make_unique<LinuxPlatformHandle>(fd);
}

bool LinuxPlatformDevice::UdmabufCreate(int udmabuf_fd, int mem_fd, uint64_t page_start_index,
                                        uint64_t page_count, int* dma_buf_fd_out) {
  struct udmabuf_create create = {.memfd = static_cast<uint32_t>(mem_fd),
                                  .flags = 0,
                                  .offset = page_start_index * magma::page_size(),
                                  .size = page_count * magma::page_size()};

  int dma_buf_fd = ioctl(udmabuf_fd, UDMABUF_CREATE, &create);
  if (dma_buf_fd < 0)
    return DRETF(false, "ioctl failed: %s", strerror(errno));

  *dma_buf_fd_out = dma_buf_fd;
  return true;
}

bool LinuxPlatformDevice::MagmaMapPageRangeBus(int device_fd, int dma_buf_fd,
                                               uint64_t start_page_index, uint64_t page_count,
                                               uint64_t* token_out, uint64_t* bus_addr_out) {
  struct magma_map_page_range_bus param = {.dma_buf_fd = dma_buf_fd,
                                           .start_page_index = start_page_index,
                                           .page_count = page_count,
                                           .token = 0,
                                           .bus_addr = bus_addr_out};

  if (ioctl(device_fd, DRM_IOCTL_MAGMA_MAP_PAGE_RANGE_BUS, &param) != 0)
    return DRETF(false, "ioctl failed: %s", strerror(errno));

  *token_out = param.token;
  return true;
}

bool LinuxPlatformDevice::MagmaMapGpu(int device_fd, bool map, uint64_t gpu_addr, uint32_t token) {
  struct magma_map_gpu param = {.map = map ? 1 : 0, .gpu_addr = gpu_addr, .token = token};

  if (ioctl(device_fd, DRM_IOCTL_MAGMA_MAP_GPU, &param) != 0)
    return DRETF(false, "ioctl failed: %s", strerror(errno));

  return true;
}

bool LinuxPlatformDevice::MagmaGetParam(int device_fd, MagmaGetParamKey key, uint64_t* value_out) {
  struct magma_param param = {.key = static_cast<uint64_t>(key)};

  if (ioctl(device_fd, DRM_IOCTL_MAGMA_GET_PARAM, &param) != 0)
    return DRETF(false, "ioctl failed: %s", strerror(errno));

  *value_out = param.value;
  return true;
}

void LinuxPlatformDevice::MagmaResetGmu(int device_fd) {
  int ret = ioctl(device_fd, DRM_IOCTL_MAGMA_RESET_GMU, nullptr);
  if (ret != 0) {
    MAGMA_LOG(WARNING, "DRM_IOCTL_MAGMA_RESET_GMU failed: %d", ret);
  }
}

std::unique_ptr<PlatformMmio> LinuxPlatformDevice::CpuMapMmio(
    unsigned int index, PlatformMmio::CachePolicy cache_policy) {
  if (cache_policy != PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE)
    return DRETP(nullptr, "Unsupported cache policy");

  uint64_t length;
  if (!MagmaGetParam(handle_.get(), MagmaGetParamKey::kRegisterSize, &length))
    return DRETP(nullptr, "MagmaGetParam failed");

  void* cpu_addr = mmap(nullptr,  // desired addr
                        length, PROT_READ | PROT_WRITE, MAP_SHARED, handle_.get(),
                        0  // offset
  );

  if (cpu_addr == MAP_FAILED)
    return DRETP(nullptr, "mmap failed");

  return std::make_unique<LinuxPlatformMmio>(cpu_addr, length);
}

Status LinuxPlatformDevice::LoadFirmware(const char* filename,
                                         std::unique_ptr<PlatformBuffer>* firmware_out,
                                         uint64_t* size_out) const {
  int fd = open(filename, O_RDONLY);
  if (fd < 0)
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Open firmware (%s) failed: %s", filename,
                    strerror(errno));

  LinuxPlatformHandle firmware(fd);

  struct stat stat;
  int ret = fstat(firmware.get(), &stat);
  if (ret != 0)
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "fstat failed: %d", ret);

  int size = stat.st_size;
  DASSERT(size > 0);

  auto buffer = magma::PlatformBuffer::Create(size, filename);
  if (!buffer)
    return DRET_MSG(MAGMA_STATUS_MEMORY_ERROR, "Couldn't create firmware buffer size %zx",
                    stat.st_size);

  void* dst;
  if (!buffer->MapCpu(&dst))
    return DRET_MSG(MAGMA_STATUS_MEMORY_ERROR, "Couldn't map firmware buffer");

  int bytes_read = pread(firmware.get(), dst, size, 0);
  if (bytes_read != size)
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "unexpected bytes_read %d != size %d", bytes_read,
                    size);

  buffer->UnmapCpu();

  *size_out = size;
  *firmware_out = std::move(buffer);

  return MAGMA_STATUS_OK;
}

std::unique_ptr<PlatformDevice> PlatformDevice::Create(void* device_handle) {
  if (!device_handle)
    return DRETP(nullptr, "device_handle is null, cannot create PlatformDevice");

  int fd = reinterpret_cast<intptr_t>(device_handle);

  return std::unique_ptr<LinuxPlatformDevice>(new LinuxPlatformDevice(LinuxPlatformHandle(fd)));
}

}  // namespace magma
