// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "linux_platform_buffer.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <map>
#include <mutex>

#include <asm/unistd.h>
#include <linux/memfd.h>

namespace {

// Defined in linux/fcntl.h, but conflicts with fcntl.h
#define F_ADD_SEALS 1033

#define F_SEAL_SEAL 0x0001
#define F_SEAL_SHRINK 0x0002

static int memfd_create(const char* name, unsigned int flags) {
  return syscall(__NR_memfd_create, name, flags);
}

}  // namespace

namespace magma {

std::unique_ptr<PlatformBuffer::MappingAddressRange> PlatformBuffer::MappingAddressRange::Create(
    std::unique_ptr<magma::PlatformHandle> handle) {
  return DRETP(nullptr, "PlatformBuffer::MappingAddressRange::Create not supported");
}

bool LinuxPlatformBuffer::duplicate_handle(uint32_t* handle_out) const {
  int fd = dup(memfd_);
  if (fd < 0)
    return DRETF(false, "dup failed: %d", errno);

  *handle_out = fd;
  return true;
}

bool LinuxPlatformBuffer::CommitPages(uint32_t start_page_index, uint32_t page_count) const {
  return DRETF(false, "Commit not supported");
}

bool LinuxPlatformBuffer::MapCpu(void** addr_out, uintptr_t alignment) {
  if (alignment)
    return DRETF(false, "Alignment not supported");

  if (map_count_ == 0) {
    void* addr = mmap(nullptr,  // addr
                      size_, PROT_READ | PROT_WRITE, MAP_SHARED, memfd_,
                      0  // offset
    );
    if (addr == MAP_FAILED)
      return DRETF(false, "mmap failed");
    virt_addr_ = addr;
  }

  *addr_out = virt_addr_;
  ++map_count_;
  return true;
}

bool LinuxPlatformBuffer::UnmapCpu() {
  switch (map_count_) {
    case 0:
      return DRETF(false, "already unmapped");
    case 1: {
      if (munmap(virt_addr_, size_) < 0)
        return DRETF(false, "munmap failed: %d", errno);
      virt_addr_ = nullptr;
      break;
    }
    default:
      break;
  }
  --map_count_;
  return true;
}

bool LinuxPlatformBuffer::MapAtCpuAddr(uint64_t addr, uint64_t offset, uint64_t length) {
  return DRETF(false, "MapAtCpuAddr not supported");
}

bool LinuxPlatformBuffer::MapCpuWithFlags(uint64_t offset, uint64_t length, uint64_t flags,
                                          std::unique_ptr<Mapping>* mapping_out) {
  return DRETF(false, "MapCpuWithFlags not supported");
}

bool LinuxPlatformBuffer::CleanCache(uint64_t offset, uint64_t size, bool invalidate) {
  return DRETF(false, "CleanCache not supported");
}

bool LinuxPlatformBuffer::SetCachePolicy(magma_cache_policy_t cache_policy) {
  return DRETF(false, "SetCachePolicy not supported");
}

magma_status_t LinuxPlatformBuffer::GetCachePolicy(magma_cache_policy_t* cache_policy_out) {
  return DRET(MAGMA_STATUS_UNIMPLEMENTED);
}

magma_status_t LinuxPlatformBuffer::GetIsMappable(magma_bool_t* is_mappable_out) {
  return DRET(MAGMA_STATUS_UNIMPLEMENTED);
}

magma::Status LinuxPlatformBuffer::SetMappingAddressRange(
    std::unique_ptr<PlatformBuffer::MappingAddressRange> address_range) {
  return DRET(MAGMA_STATUS_UNIMPLEMENTED);
}

bool LinuxPlatformBuffer::Read(void* buffer, uint64_t offset, uint64_t length) {
  ssize_t bytes_read = pread(memfd_, buffer, length, offset);
  if (bytes_read < 0)
    return DRETF(false, "pread failed: %d", errno);
  if (static_cast<uint64_t>(bytes_read) != length)
    return DRETF(false, "pread length mismatch: %zd != %lu", bytes_read, length);
  return true;
}

bool LinuxPlatformBuffer::Write(const void* buffer, uint64_t offset, uint64_t length) {
  if (offset + length > size_)
    return DRETF(false, "offset %lu + length %lu > size %lu", offset, length, size_);
  ssize_t bytes_written = pwrite(memfd_, buffer, length, offset);
  if (bytes_written < 0)
    return DRETF(false, "pwrite failed: %d", errno);
  if (static_cast<uint64_t>(bytes_written) != length)
    return DRETF(false, "pwrite length mismatch: %zd != %lu", bytes_written, length);
  return true;
}

std::unique_ptr<PlatformBuffer> PlatformBuffer::Create(uint64_t size, const char* name) {
  if (size == 0)
    return nullptr;

  size = magma::round_up(size, magma::page_size());

  int memfd = memfd_create(name, MFD_ALLOW_SEALING);
  if (memfd < 0)
    return DRETP(nullptr, "memfd_create failed: %d", errno);

  if (ftruncate(memfd, size) < 0) {
    close(memfd);
    return DRETP(nullptr, "ftruncate failed: %d", errno);
  }

  if (fcntl(memfd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_SEAL) < 0)
    return DRETP(nullptr, "fcntl failed: %d", errno);

  struct stat st;
  if (fstat(memfd, &st) < 0)
    return DRETP(nullptr, "fstat failed: %d", errno);

  // Transfer ownership of memfd
  return std::make_unique<LinuxPlatformBuffer>(memfd, st.st_ino, size);
}

std::unique_ptr<PlatformBuffer> PlatformBuffer::Import(uint32_t handle) {
  int fd = handle;

  struct stat st;
  if (fstat(fd, &st) < 0)
    return DRETP(nullptr, "fstat failed: %d", errno);

  return std::make_unique<LinuxPlatformBuffer>(fd, st.st_ino, st.st_size);
}

}  // namespace magma
