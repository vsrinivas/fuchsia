// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <lib/sysconfig/sync-client.h>
// clang-format on

#include <dirent.h>
#include <fcntl.h>

#include <fbl/algorithm.h>
#include <fuchsia/sysinfo/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/watcher.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <string.h>
#include <zircon/hw/gpt.h>

namespace sysconfig {

namespace skipblock = ::llcpp::fuchsia::hardware::skipblock;

namespace {

constexpr size_t kKilobyte = 1 << 10;
constexpr size_t kSysconfigSize = 60 * kKilobyte;
constexpr size_t kABRMetadtaSize = 4 * kKilobyte;
constexpr size_t kVerifiedBootMetadataSize = 64 * kKilobyte;

struct AstroSysconfigPartition {
  uint8_t sysconfig[kSysconfigSize];
  uint8_t abr_metadata[kABRMetadtaSize];
  uint8_t vb_metadata_a[kVerifiedBootMetadataSize];
  uint8_t vb_metadata_b[kVerifiedBootMetadataSize];
  uint8_t vb_metadata_r[kVerifiedBootMetadataSize];
} __PACKED;

static_assert(sizeof(AstroSysconfigPartition) == 256 * kKilobyte);

zx_status_t FindSysconfigPartition(const fbl::unique_fd& devfs_root,
                                   std::optional<skipblock::SkipBlock::SyncClient>* out) {
  fbl::unique_fd dir_fd(openat(devfs_root.get(), "class/skip-block/", O_RDONLY));
  if (!dir_fd) {
    return ZX_ERR_IO;
  }
  DIR* dir = fdopendir(dir_fd.release());
  if (dir == nullptr) {
    return ZX_ERR_IO;
  }
  const auto closer = fbl::MakeAutoCall([&dir]() { closedir(dir); });

  auto watch_dir_event_cb = [](int dirfd, int event, const char* filename, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
      return ZX_OK;
    }
    if ((strcmp(filename, ".") == 0) || strcmp(filename, "..") == 0) {
      return ZX_OK;
    }

    zx::channel local, remote;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
      return status;
    }

    fdio_cpp::UnownedFdioCaller caller(dirfd);
    status = fdio_service_connect_at(caller.borrow_channel(), filename, remote.release());
    if (status != ZX_OK) {
      return ZX_OK;
    }
    skipblock::SkipBlock::SyncClient skip_block(std::move(local));
    auto result = skip_block.GetPartitionInfo();
    status = result.ok() ? result.value().status : result.status();
    if (status != ZX_OK) {
      return ZX_OK;
    }
    const auto& response = result.value();
    const uint8_t type[] = GUID_SYS_CONFIG_VALUE;
    if (memcmp(response.partition_info.partition_guid.data(), type, skipblock::GUID_LEN) != 0) {
      return ZX_OK;
    }

    auto* out = static_cast<std::optional<skipblock::SkipBlock::SyncClient>*>(cookie);
    *out = std::move(skip_block);
    return ZX_ERR_STOP;
  };


  const zx::time deadline = zx::deadline_after(zx::sec(5));
  if (fdio_watch_directory(dirfd(dir), watch_dir_event_cb, deadline.get(), out) != ZX_ERR_STOP) {
    return ZX_ERR_NOT_FOUND;
  }
  return ZX_OK;
}

zx_status_t CheckIfAstro(const fbl::unique_fd& devfs_root) {
  fdio_cpp::UnownedFdioCaller caller(devfs_root.get());
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }

  status = fdio_service_connect_at(caller.borrow_channel(), "sys/platform", remote.release());
  if (status != ZX_OK) {
    return ZX_OK;
  }

  auto result = ::llcpp::fuchsia::sysinfo::SysInfo::Call::GetBoardName(zx::unowned(local));
  status = result.ok() ? result->status : result.status();
  if (status != ZX_OK) {
    return status;
  }
  if (strncmp(result->name.data(), "astro", result->name.size()) == 0) {
    return ZX_OK;
  }

  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace

zx_status_t SyncClient::Create(std::optional<SyncClient>* out) {
  fbl::unique_fd devfs_root(open("/dev", O_RDWR));
  if (!devfs_root) {
    return ZX_ERR_IO;
  }
  return Create(devfs_root, out);
}

zx_status_t SyncClient::Create(const fbl::unique_fd& devfs_root, std::optional<SyncClient>* out) {
  // TODO(surajmalhotra): This is just a temporary measure to allow us to hardcode constants into
  // this library safely. For future products, the library should be updated to use some sort of
  // configuration file to determine partition layout.
  auto status = CheckIfAstro(devfs_root);
  if (status != ZX_OK) {
    return status;
  }

  std::optional<skipblock::SkipBlock::SyncClient> skip_block;
  status = FindSysconfigPartition(devfs_root, &skip_block);
  if (status != ZX_OK) {
    return status;
  }

  *out = SyncClient(*std::move(skip_block));

  return ZX_OK;
}

zx_status_t SyncClient::WritePartition(PartitionType partition, const zx::vmo& vmo,
                                       zx_off_t vmo_offset) {
  zx::vmo dup;
  if (zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup); status != ZX_OK) {
    return status;
  }
  skipblock::WriteBytesOperation operation = {
      .vmo = std::move(dup),
      .vmo_offset = vmo_offset,
      .offset = GetPartitionOffset(partition),
      .size = GetPartitionSize(partition),
  };
  auto result = skip_block_.WriteBytes(std::move(operation));
  return result.ok() ? result.value().status : result.status();
}

zx_status_t SyncClient::InitializeReadMapper() {
  auto result = skip_block_.GetPartitionInfo();
  zx_status_t status = result.ok() ? result.value().status : result.status();
  if (status != ZX_OK) {
    return status;
  }
  const uint64_t block_size = result.value().partition_info.block_size_bytes;

  return read_mapper_.CreateAndMap(fbl::round_up(block_size, ZX_PAGE_SIZE), "sysconfig read");
}

zx_status_t SyncClient::ReadPartition(PartitionType partition, const zx::vmo& vmo,
                                      zx_off_t vmo_offset) {
  // Lazily create read mapper.
  if (read_mapper_.start() == nullptr) {
    zx_status_t status = InitializeReadMapper();
    if (status != ZX_OK) {
      return status;
    }
  }

  zx::vmo dup;
  if (zx_status_t status = read_mapper_.vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
      status != ZX_OK) {
    return status;
  }
  skipblock::ReadWriteOperation operation = {
      .vmo = std::move(dup),
      .vmo_offset = 0,
      .block = 0,
      .block_count = 1,
  };
  auto result = skip_block_.Read(std::move(operation));
  zx_status_t status = result.ok() ? result.value().status : result.status();
  if (status != ZX_OK) {
    return status;
  }

  return vmo.write(reinterpret_cast<uint8_t*>(read_mapper_.start()) + GetPartitionOffset(partition),
                   vmo_offset, GetPartitionSize(partition));
}

size_t SyncClient::GetPartitionSize(PartitionType partition) {

  switch (partition) {
    case PartitionType::kSysconfig:
      return kSysconfigSize;
    case PartitionType::kABRMetadata:
      return kABRMetadtaSize;
    case PartitionType::kVerifiedBootMetadataA:
    case PartitionType::kVerifiedBootMetadataB:
    case PartitionType::kVerifiedBootMetadataR:
      return kVerifiedBootMetadataSize;
  }
  ZX_ASSERT(false);  // Unreachable.
}

size_t SyncClient::GetPartitionOffset(PartitionType partition) {
  switch (partition) {
    case PartitionType::kSysconfig:
      return offsetof(AstroSysconfigPartition, sysconfig);
    case PartitionType::kABRMetadata:
      return offsetof(AstroSysconfigPartition, abr_metadata);
    case PartitionType::kVerifiedBootMetadataA:
      return offsetof(AstroSysconfigPartition, vb_metadata_a);
    case PartitionType::kVerifiedBootMetadataB:
      return offsetof(AstroSysconfigPartition, vb_metadata_b);
    case PartitionType::kVerifiedBootMetadataR:
      return offsetof(AstroSysconfigPartition, vb_metadata_r);
  }
  ZX_ASSERT(false);  // Unreachable.
}

}  // namespace sysconfig
