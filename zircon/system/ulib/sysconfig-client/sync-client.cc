// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <lib/sysconfig/sync-client.h>
#include <lib/sysconfig/sysconfig-header.h>
// clang-format on

#include <array>
#include <dirent.h>
#include <fcntl.h>

#include <fbl/algorithm.h>
#include <fuchsia/sysinfo/llcpp/fidl.h>
#include <lib/cksum.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/watcher.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <string.h>
#include <zircon/hw/gpt.h>
#include <zircon/status.h>

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

constexpr size_t kAstroSysconfigPartitionSize = sizeof(AstroSysconfigPartition);

const sysconfig_header kLegacyLayout = {
    .magic = SYSCONFIG_HEADER_MAGIC_ARRAY,
    .sysconfig_data = {offsetof(AstroSysconfigPartition, sysconfig), kSysconfigSize},
    .abr_metadata = {offsetof(AstroSysconfigPartition, abr_metadata), kABRMetadtaSize},
    .vb_metadata_a = {offsetof(AstroSysconfigPartition, vb_metadata_a), kVerifiedBootMetadataSize},
    .vb_metadata_b = {offsetof(AstroSysconfigPartition, vb_metadata_b), kVerifiedBootMetadataSize},
    .vb_metadata_r = {offsetof(AstroSysconfigPartition, vb_metadata_r), kVerifiedBootMetadataSize},
    .crc_value = 2716817057,  // pre-calculated crc
};

constexpr size_t kAstroPageSize = 4 * kKilobyte;

constexpr zx_vm_option_t kVmoRw = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;

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
  // NOTE(brunodalbo): An older version of this routine used
  // fdio_connect_at(borrowed_channel_from_devfs_root, ...). The problem is that borrowing a channel
  // from a file descriptor to /dev created from a sandbox component is invalid, since /dev is not
  // part of its flat namespace. Here we use `openat` and only borrow the channel later, when it's
  // guaranteed to be backed by a remote service.
  fbl::unique_fd platform_fd(openat(devfs_root.get(), "sys/platform", O_RDWR));
  if (!platform_fd) {
    return ZX_ERR_IO;
  }
  fdio_cpp::FdioCaller caller(std::move(platform_fd));
  if (!caller) {
    return ZX_ERR_IO;
  }
  auto result = ::llcpp::fuchsia::sysinfo::SysInfo::Call::GetBoardName(caller.channel());
  zx_status_t status = result.ok() ? result->status : result.status();
  if (status != ZX_OK) {
    return status;
  }
  if (strncmp(result->name.data(), "astro", result->name.size()) == 0) {
    return ZX_OK;
  }

  return ZX_ERR_NOT_SUPPORTED;
}

// <memory> should be the starting address of a sysconfig_header structure.
// Since header is located at page 0, it can also be the starting address of the partition.
// If the header in the memory is not valid, it will return the default header provided by the
// caller.
std::unique_ptr<sysconfig_header> ParseHeader(const void* memory,
                                              const sysconfig_header& default_header) {
  auto ret = std::make_unique<sysconfig_header>();
  memcpy(ret.get(), memory, sizeof(sysconfig_header));
  if (!sysconfig_header_valid(ret.get(), kAstroPageSize, kAstroSysconfigPartitionSize)) {
    fprintf(stderr, "ParseHeader: Falling back to default header.\n");
    memcpy(ret.get(), &default_header, sizeof(sysconfig_header));
  }

  return ret;
}

sysconfig_subpartition GetSubpartitionInfo(const sysconfig_header& header,
                                           SyncClient::PartitionType partition) {
  switch (partition) {
    case SyncClient::PartitionType::kSysconfig:
      return header.sysconfig_data;
    case SyncClient::PartitionType::kABRMetadata:
      return header.abr_metadata;
    case SyncClient::PartitionType::kVerifiedBootMetadataA:
      return header.vb_metadata_a;
    case SyncClient::PartitionType::kVerifiedBootMetadataB:
      return header.vb_metadata_b;
    case SyncClient::PartitionType::kVerifiedBootMetadataR:
      return header.vb_metadata_r;
  }
  ZX_ASSERT(false);  // Unreachable.
}

struct PartitionTypeAndInfo {
  SyncClient::PartitionType id;
  uint64_t offset;
  uint64_t size;

  PartitionTypeAndInfo() {}

  // Why not use a sysconfig_subpartition member?
  // Because sysconfig_subpartition is declared with "packed" atrribute. This has been seen
  // to cause alignment issue on some platforms if used directly as member.
  PartitionTypeAndInfo(SyncClient::PartitionType part, sysconfig_subpartition info)
      : id(part), offset(info.offset), size(info.size) {}
};

using PartitionTypeAndInfoArray = std::array<PartitionTypeAndInfo, 5>;

PartitionTypeAndInfoArray SortSubpartitions(const sysconfig_header& header) {
  using PartitionType = SyncClient::PartitionType;
  PartitionTypeAndInfoArray ret = {{
      PartitionTypeAndInfo(PartitionType::kSysconfig, header.sysconfig_data),
      PartitionTypeAndInfo(PartitionType::kABRMetadata, header.abr_metadata),
      PartitionTypeAndInfo(PartitionType::kVerifiedBootMetadataA, header.vb_metadata_a),
      PartitionTypeAndInfo(PartitionType::kVerifiedBootMetadataB, header.vb_metadata_b),
      PartitionTypeAndInfo(PartitionType::kVerifiedBootMetadataR, header.vb_metadata_r),
  }};
  std::sort(ret.begin(), ret.end(),
            [](const auto& l, const auto& r) { return l.offset < r.offset; });
  return ret;
}

// Rotate <mem> to the left by <rotate> unit
void RotateMemoryLeft(uint8_t* mem, size_t len, size_t rotate) {
  /**
   * Example: rotate "123456789" 3 units to the left, into "456789123"
   *
   * Step 1: reverse the entire array -> "987654321"
   * Step 2: reverse the first (9 - 3) units -> "456789321"
   * Step 3: reverse the last 3 units -> "456789123"
   */
  std::reverse(mem, mem + len);
  std::reverse(mem, mem + len - rotate);
  std::reverse(mem + len - rotate, mem + len);
}

struct PartitionTransformInfo {
  PartitionTypeAndInfoArray current;
  PartitionTypeAndInfoArray target_sorted;

  PartitionTransformInfo(const sysconfig_header& current_header,
                         const sysconfig_header& target_header) {
    current = SortSubpartitions(current_header);
    target_sorted = SortSubpartitions(target_header);
  }

  PartitionTypeAndInfo GetCurrentInfo(SyncClient::PartitionType id) {
    auto res = std::find_if(current.begin(), current.end(),
                            [id](const auto& ele) { return ele.id == id; });
    ZX_ASSERT(res != current.end());
    return *res;
  }

  PartitionTypeAndInfo GetTargetInfo(SyncClient::PartitionType id) {
    auto res = std::find_if(target_sorted.begin(), target_sorted.end(),
                            [id](const auto& ele) { return ele.id == id; });
    ZX_ASSERT(res != target_sorted.end());
    return *res;
  }
};

// Update sysconfig layout according to a new header in place.
void UpdateSysconfigLayout(void* start, size_t len, const sysconfig_header& current_header,
                           const sysconfig_header& target_header) {
  /**
   * Example:
   * Existing layout: AAAAAABCCCCCCCC
   * Target layout:   XAACCCCCCCCBBBB ('X' means not belonging to any sub-partition)
   *
   * Step 1: Shrink partition A -> "AAXXXXBCCCCCCCC"
   * Step 2: Pack all sub-partitions to the right -> "XXXXAABCCCCCCCC"
   * Step 3: Reorder sub-partitions in packed region -> "XXXXAACCCCCCCCB"
   * Step 4: Move all sub-partitions to target offset in their order. Size of B increases
   * naturally. -> "XAACCCCCCCCBXXX"
   *
   * The average run-time of the algorithm is approximately 2ms in release build.
   */

  auto mem = static_cast<uint8_t*>(start);
  PartitionTransformInfo info(current_header, target_header);

  // Step 1: Shrink sub-partitions sizes to target.
  for (auto& current : info.current) {
    current.size = std::min(current.size, info.GetTargetInfo(current.id).size);
  }

  // Step 2: Pack all sub-partitions to the right.
  size_t move_right_copy_index = len;
  for (auto iter = info.current.rbegin(); iter != info.current.rend(); iter++) {
    move_right_copy_index -= iter->size;
    memmove(&mem[move_right_copy_index], &mem[iter->offset], iter->size);
    iter->offset = move_right_copy_index;
  }

  // Step 3: Order sub-partitions according to the target header, by rotating the memory.
  size_t seg_offset = move_right_copy_index;
  for (auto& target : info.target_sorted) {
    PartitionTypeAndInfo target_part_in_current = info.GetCurrentInfo(target.id);
    // Rotate this sub-partition to the first position in the segment.
    uint8_t* seg_start = &mem[seg_offset];
    size_t seg_len = len - seg_offset;
    size_t rotate = target_part_in_current.offset - seg_offset;
    if (rotate != 0) {
      RotateMemoryLeft(seg_start, seg_len, rotate);
      // Update offset after rotation.
      for (auto& current : info.current) {
        if (current.offset >= seg_offset) {
          current.offset = (current.offset - seg_offset + seg_len - rotate) % seg_len + seg_offset;
        }
      }
    }
    seg_offset += target_part_in_current.size;
  }

  // Step 4: Move sub-partitions to their target offsets. Sizes increase naturally.
  size_t end_of_prev_part = 0;
  for (auto& target : info.target_sorted) {
    PartitionTypeAndInfo target_part_in_current = info.GetCurrentInfo(target.id);
    memset(&mem[end_of_prev_part], 0xff, target.offset - end_of_prev_part);
    if (target.offset != target_part_in_current.offset) {
      memmove(&mem[target.offset], &mem[target_part_in_current.offset],
              target_part_in_current.size);
    }
    // Note: We set end_or_prev_part to be target offset + old size so that
    // if target size > old size, next iteration will set the expanded memory to 0xff.
    end_of_prev_part = target.offset + target_part_in_current.size;
  }
  // Set the remaining part after all sub-partitions of 0xff, if there is any.
  memset(&mem[end_of_prev_part], 0xff, len - end_of_prev_part);
}

}  // namespace

zx_status_t SyncClient::Create(std::optional<SyncClient>* out) {
  fbl::unique_fd devfs_root(open("/dev", O_RDONLY));
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

const sysconfig_header* SyncClient::GetHeader(zx_status_t* status_out) {
  if (header_) {
    return header_.get();
  }

  if (auto status = LoadFromStorage(); status != ZX_OK) {
    fprintf(stderr, "ReadHeader: Failed to read header from storage. %s.\n",
            zx_status_get_string(status));
    if (status_out) {
      *status_out = status;
    }
    return nullptr;
  }

  header_ = ParseHeader(read_mapper_.start(), kLegacyLayout);
  return header_.get();
}

zx_status_t SyncClient::WritePartition(PartitionType partition, const zx::vmo& vmo,
                                       zx_off_t vmo_offset) {
  const sysconfig_header* header;
  zx_status_t status;
  if ((header = GetHeader(&status)) == nullptr) {
    // In case there is acutally a valid header in the storage, but just that we fail to read due to
    // transient error, refuse to perform any write to avoid compromising the partition, bootloader
    // etc.
    fprintf(stderr,
            "WritePartition: error while reading for header. Refuse to perform any write. %s\n",
            zx_status_get_string(status));
    return status;
  }

  auto partition_info = GetSubpartitionInfo(*header, partition);
  return Write(partition_info.offset, partition_info.size, vmo, vmo_offset);
}

zx_status_t SyncClient::Write(size_t offset, size_t len, const zx::vmo& vmo, zx_off_t vmo_offset) {
  zx::vmo dup;
  if (zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup); status != ZX_OK) {
    return status;
  }
  skipblock::WriteBytesOperation operation = {
      .vmo = std::move(dup),
      .vmo_offset = vmo_offset,
      .offset = offset,
      .size = len,
  };
  printf("sysconfig: ADDING ERASE CYCLE TO SYSCONFIG\n");
  auto result = skip_block_.WriteBytes(std::move(operation));
  return result.ok() ? result.value().status : result.status();
}

zx_status_t SyncClient::WriteBytesWithoutErase(size_t offset, size_t len, const zx::vmo& vmo,
                                               zx_off_t vmo_offset) {
  zx::vmo dup;
  if (zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup); status != ZX_OK) {
    return status;
  }
  skipblock::WriteBytesOperation operation = {
      .vmo = std::move(dup),
      .vmo_offset = vmo_offset,
      .offset = offset,
      .size = len,
  };
  auto result = skip_block_.WriteBytesWithoutErase(std::move(operation));
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
  const sysconfig_header* header;
  zx_status_t status;
  if ((header = GetHeader(&status)) == nullptr) {
    fprintf(stderr, "ReadPartition: error while reading for header. %s\n",
            zx_status_get_string(status));
    return status;
  }

  auto partition_info = GetSubpartitionInfo(*header, partition);
  return Read(partition_info.offset, partition_info.size, vmo, vmo_offset);
}

zx_status_t SyncClient::Read(size_t offset, size_t len, const zx::vmo& vmo, zx_off_t vmo_offset) {
  if (auto status = LoadFromStorage(); status != ZX_OK) {
    fprintf(stderr, "Failed to read content from storage. %s\n", zx_status_get_string(status));
    return status;
  }
  return vmo.write(reinterpret_cast<uint8_t*>(read_mapper_.start()) + offset, vmo_offset, len);
}

zx_status_t SyncClient::LoadFromStorage() {
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

  return ZX_OK;
}

zx_status_t SyncClient::GetPartitionSize(PartitionType partition, size_t* out) {
  const sysconfig_header* header;
  zx_status_t status;
  if ((header = GetHeader(&status)) == nullptr) {
    fprintf(stderr, "GetPartitionSize: error while reading for header. %s\n",
            zx_status_get_string(status));
    return status;
  }
  *out = GetSubpartitionInfo(*header, partition).size;
  return ZX_OK;
}

zx_status_t SyncClient::GetPartitionOffset(PartitionType partition, size_t* out) {
  const sysconfig_header* header;
  zx_status_t status;
  if ((header = GetHeader(&status)) == nullptr) {
    fprintf(stderr, "GetPartitionOffset: error while reading for header. %s\n",
            zx_status_get_string(status));
    return status;
  }
  *out = GetSubpartitionInfo(*header, partition).offset;
  return ZX_OK;
}

zx_status_t SyncClient::UpdateLayout(const sysconfig_header& target_header) {
  zx_status_t status_get_header;
  auto current_header = GetHeader(&status_get_header);
  if (current_header == nullptr) {
    fprintf(stderr, "UpdateLayout: Failed to read current header. %s\n",
            zx_status_get_string(status_get_header));
    return status_get_header;
  }

  if (sysconfig_header_equal(&target_header, current_header)) {
    fprintf(stderr,
            "UpdateLayout: Already orgianized according to the specified layout. Skipping.\n");
    return ZX_OK;
  }

  sysconfig_header header = target_header;
  update_sysconfig_header_magic_and_crc(&header);

  // Refuse to update to an invalid header in the first place
  if (!sysconfig_header_valid(&header, kAstroPageSize, kAstroSysconfigPartitionSize)) {
    fprintf(stderr, "UpdateLayout: Header is invalid. Refuse to update\n");
    return ZX_ERR_INVALID_ARGS;
  }

  // Read the entire partition in to read_mapper_
  if (auto status = LoadFromStorage(); status != ZX_OK) {
    fprintf(stderr, "UpdateLayout: Failed to load from storage. %s\n",
            zx_status_get_string(status));
    return status;
  }

  UpdateSysconfigLayout(read_mapper_.start(), read_mapper_.size(), *current_header, target_header);

  // Write the header, if it is not the legacy one.
  zx_status_t status_write;
  if (!sysconfig_header_equal(&header, &kLegacyLayout) &&
      (status_write = read_mapper_.vmo().write(&header, 0, sizeof(header))) != ZX_OK) {
    fprintf(stderr, "failed to write header to vmo. %s\n", zx_status_get_string(status_write));
    return status_write;
  }

  if (auto status = Write(0, kAstroSysconfigPartitionSize, read_mapper_.vmo(), 0);
      status != ZX_OK) {
    fprintf(stderr, "UpdateLayout: failed to write to storage. %s\n", zx_status_get_string(status));
    return status;
  }

  header_ = std::make_unique<sysconfig_header>(header);
  return ZX_OK;
}

zx_status_t SyncClientBuffered::GetPartitionSize(PartitionType partition, size_t* size) {
  return client_.GetPartitionSize(partition, size);
}

zx_status_t SyncClientBuffered::GetPartitionOffset(PartitionType partition, size_t* size) {
  return client_.GetPartitionOffset(partition, size);
}

uint32_t SyncClientBuffered::PartitionTypeToCacheMask(PartitionType partition) {
  switch (partition) {
    case PartitionType::kSysconfig:
      return CacheBitMask::kSysconfig;
    case PartitionType::kABRMetadata:
      return CacheBitMask::kAbrMetadata;
    case PartitionType::kVerifiedBootMetadataA:
      return CacheBitMask::kVbmetaA;
    case PartitionType::kVerifiedBootMetadataB:
      return CacheBitMask::kVbmetaB;
    case PartitionType::kVerifiedBootMetadataR:
      return CacheBitMask::kVbmetaR;
  }
  ZX_ASSERT_MSG(false, "Unknown partition type %d\n", static_cast<int>(partition));
}

bool SyncClientBuffered::IsCacheEmpty(PartitionType partition) {
  return (cache_modified_flag_ & PartitionTypeToCacheMask(partition)) == 0;
}

void SyncClientBuffered::MarkCacheNonEmpty(PartitionType partition) {
  cache_modified_flag_ |= PartitionTypeToCacheMask(partition);
}

zx_status_t SyncClientBuffered::CreateCache() {
  if (cache_.vmo()) {
    return ZX_OK;
  }

  if (auto status = cache_.CreateAndMap(kAstroSysconfigPartitionSize, "sysconfig cache", kVmoRw);
      status != ZX_OK) {
    fprintf(stderr, "failed to create cache. %s\n", zx_status_get_string(status));
    return status;
  }

  if (auto status = client_.Read(0, kAstroSysconfigPartitionSize, cache_.vmo(), 0);
      status != ZX_OK) {
    fprintf(stderr, "failed to initialize cache content. %s\n", zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

void SyncClientBuffered::InvalidateCache() {
  cache_modified_flag_ = 0;
  cache_.Reset();
}

bool SyncClientBuffered::IsAllCacheEmpty() { return cache_modified_flag_ == 0; }

zx_status_t SyncClientBuffered::WritePartition(PartitionType partition, const zx::vmo& vmo,
                                               zx_off_t vmo_offset) {
  return WriteCache(partition, vmo, vmo_offset);
}

zx_status_t SyncClientBuffered::WriteCache(PartitionType partition, const zx::vmo& vmo,
                                           zx_off_t vmo_offset) {
  if (auto status = CreateCache(); status != ZX_OK) {
    return status;
  }

  size_t size;
  uint8_t* start;
  if (auto status = GetSubpartitionCacheAddrSize(partition, &start, &size); status != ZX_OK) {
    return status;
  }

  if (auto status = vmo.read(start, vmo_offset, size); status != ZX_OK) {
    fprintf(stderr, "WriteCache: Failed to write to cache. %s\n", zx_status_get_string(status));
    return status;
  }
  MarkCacheNonEmpty(partition);

  return ZX_OK;
}

zx_status_t SyncClientBuffered::ReadPartition(PartitionType partition, const zx::vmo& vmo,
                                              zx_off_t vmo_offset) {
  return IsCacheEmpty(partition) ? client_.ReadPartition(partition, vmo, vmo_offset)
                                 : ReadCache(partition, vmo, vmo_offset);
}

zx_status_t SyncClientBuffered::ReadCache(PartitionType partition, const zx::vmo& vmo,
                                          zx_off_t vmo_offset) {
  size_t size;
  uint8_t* start;
  if (auto status = GetSubpartitionCacheAddrSize(partition, &start, &size); status != ZX_OK) {
    return status;
  }

  if (auto status = vmo.write(start, vmo_offset, size); status != ZX_OK) {
    fprintf(stderr, "ReadCache::Failed to read from cached %d, %s\n", static_cast<int>(partition),
            zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

zx_status_t SyncClientBuffered::Flush() {
  if (IsAllCacheEmpty()) {
    return ZX_OK;
  }

  if (auto status = client_.Write(0, kAstroSysconfigPartitionSize, cache_.vmo(), 0);
      status != ZX_OK) {
    fprintf(stderr, "Failed to flush write. %s\n", zx_status_get_string(status));
    return status;
  }

  InvalidateCache();
  return ZX_OK;
}

zx_status_t SyncClientBuffered::GetSubpartitionCacheAddrSize(PartitionType partition,
                                                             uint8_t** start, size_t* size) {
  zx_status_t status_get_header;
  auto header = client_.GetHeader(&status_get_header);
  if (!header) {
    return status_get_header;
  }
  auto info = GetSubpartitionInfo(*header, partition);
  *start = static_cast<uint8_t*>(cache_.start()) + info.offset;
  *size = info.size;
  return ZX_OK;
}

const uint8_t* SyncClientBuffered::GetCacheBuffer(PartitionType partition) {
  size_t size;
  uint8_t* start;
  if (auto status = GetSubpartitionCacheAddrSize(partition, &start, &size); status != ZX_OK) {
    return nullptr;
  }
  return start;
}

zx_status_t SyncClientBuffered::UpdateLayout(const sysconfig_header& target_header) {
  if (auto status = Flush(); status != ZX_OK) {
    return status;
  }

  return client_.UpdateLayout(target_header);
}

// One example layout that supports abr wear-leveling.
// Comparing with legacy layout, abr metadata is put at the end, extended to 10 pages
// sysconfig_data shrunken to 5 pages.
// The first page is reserved for header.
static constexpr sysconfig_header kLayoutForWearLeveling = {
    .magic = SYSCONFIG_HEADER_MAGIC_ARRAY,
    .sysconfig_data = {4 * kKilobyte, 20 * kKilobyte},
    .abr_metadata = {216 * kKilobyte, 40 * kKilobyte},
    .vb_metadata_a = {24 * kKilobyte, kVerifiedBootMetadataSize},
    .vb_metadata_b = {88 * kKilobyte, kVerifiedBootMetadataSize},
    .vb_metadata_r = {152 * kKilobyte, kVerifiedBootMetadataSize},
    .crc_value = 0x16713db5,
};

const sysconfig_header& SyncClientAbrWearLeveling::GetAbrWearLevelingSupportedLayout() {
  return kLayoutForWearLeveling;
}

zx_status_t SyncClientAbrWearLeveling::ReadPartition(PartitionType partition, const zx::vmo& vmo,
                                                     zx_off_t vmo_offset) {
  return IsCacheEmpty(partition) && partition == PartitionType::kABRMetadata
             ? ReadLatestAbrMetadataFromStorage(vmo, vmo_offset)
             : SyncClientBuffered::ReadPartition(partition, vmo, vmo_offset);
}

bool SyncClientAbrWearLeveling::IsOnlyAbrMetadataModified() {
  return !IsCacheEmpty(PartitionType::kABRMetadata) && IsCacheEmpty(PartitionType::kSysconfig) &&
         IsCacheEmpty(PartitionType::kVerifiedBootMetadataA) &&
         IsCacheEmpty(PartitionType::kVerifiedBootMetadataB) &&
         IsCacheEmpty(PartitionType::kVerifiedBootMetadataR);
}

zx_status_t SyncClientAbrWearLeveling::ReadLatestAbrMetadataFromStorage(const zx::vmo& vmo,
                                                                        zx_off_t vmo_offset) {
  abr_metadata_ext latest;

  zx_status_t status_get_header;
  auto header = client_.GetHeader(&status_get_header);
  if (header == nullptr) {
    return status_get_header;
  }

  if (auto status = client_.LoadFromStorage(); status != ZX_OK) {
    return status;
  }

  auto abr_start =
      static_cast<uint8_t*>(client_.read_mapper_.start()) + header->abr_metadata.offset;

  if (layout_support_wear_leveling(header, kAstroPageSize)) {
    find_latest_abr_metadata_page(header, abr_start, kAstroPageSize, &latest);
  } else {
    memcpy(&latest, abr_start, sizeof(abr_metadata_ext));
  }

  return vmo.write(&latest, vmo_offset, sizeof(abr_metadata_ext));
}

zx_status_t SyncClientAbrWearLeveling::Flush() {
  if (IsAllCacheEmpty()) {
    return ZX_OK;
  }

  zx_status_t status_get_header;
  auto header = client_.GetHeader(&status_get_header);
  if (!header) {
    return status_get_header;
  }

  if (!layout_support_wear_leveling(header, kAstroPageSize)) {
    return SyncClientBuffered::Flush();
  }

  // Try if we can only perform an abr metadata append.
  if (FlushAppendAbrMetadata(header) != ZX_OK) {
    // If appending is not applicable, flush all valid cached data to memory.
    // An erase will be introduced.
    if (auto status = FlushReset(header); status != ZX_OK) {
      return status;
    }
  }

  InvalidateCache();
  return ZX_OK;
}

zx_status_t SyncClientAbrWearLeveling::FlushAppendAbrMetadata(const sysconfig_header* header) {
  if (!IsOnlyAbrMetadataModified()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (auto status = client_.LoadFromStorage(); status != ZX_OK) {
    return status;
  }
  auto& mapper = client_.read_mapper_;
  auto abr_start = static_cast<const uint8_t*>(mapper.start()) + header->abr_metadata.offset;

  // Find an empty page to write
  int64_t page_write_index;
  if (!find_empty_page_for_wear_leveling(header, abr_start, kAstroPageSize, &page_write_index)) {
    return ZX_ERR_INTERNAL;
  }

  // Read the abr metadata from cache, update magic and write back to cache.
  // Note: Although cache has the same layout as sysconfig partition, writing abr metadata to
  // cache does not use abr wear-leveling. The new data is just written to the start of the abr
  // metadata sub-partition in cache.
  abr_metadata_ext abr_data;
  uint8_t* cache_abr_start = static_cast<uint8_t*>(cache_.start()) + header->abr_metadata.offset;
  memcpy(&abr_data, cache_abr_start, sizeof(abr_metadata_ext));
  set_abr_metadata_ext_magic(&abr_data);
  memcpy(cache_abr_start, &abr_data, sizeof(abr_metadata_ext));

  // Perform a write without erase.
  size_t offset =
      header->abr_metadata.offset + static_cast<size_t>(page_write_index) * kAstroPageSize;
  if (auto status = client_.WriteBytesWithoutErase(offset, kAstroPageSize, cache_.vmo(),
                                                   header->abr_metadata.offset);
      status != ZX_OK) {
    fprintf(stderr, "Failed to append abr metadata to persistent storage. %s\n",
            zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

zx_status_t SyncClientAbrWearLeveling::FlushReset(const sysconfig_header* header) {
  // 1. Write the abr data to the first page of in the sub-partition
  // 2. Set the rest pages of the abr sub-partition to empty (write 0xff).

  auto abr_start = static_cast<uint8_t*>(cache_.start()) + header->abr_metadata.offset;
  abr_metadata_ext abr_data;
  // Find the latest abr meta, either from the cache if it is modified, or from the storage.
  if (IsCacheEmpty(PartitionType::kABRMetadata)) {
    find_latest_abr_metadata_page(header, abr_start, kAstroPageSize, &abr_data);
  } else {
    memcpy(&abr_data, abr_start, sizeof(abr_metadata_ext));
  }
  // Set magic.
  set_abr_metadata_ext_magic(&abr_data);
  // Put the latest abr data to the first page.
  memcpy(abr_start, &abr_data, sizeof(abr_metadata_ext));
  // Reset the rest of the pages in abr partition to be empty for future use
  memset(&abr_start[kAstroPageSize], 0xff, header->abr_metadata.size - kAstroPageSize);

  // Write data to persistent storage.
  if (auto status = client_.Write(0, kAstroSysconfigPartitionSize, cache_.vmo(), 0);
      status != ZX_OK) {
    fprintf(stderr, "Failed to flush write. %s\n", zx_status_get_string(status));
    return status;
  }

  erase_count_++;
  return ZX_OK;
}

}  // namespace sysconfig

uint32_t sysconfig_header_crc32(uint32_t crc, const uint8_t* buf, size_t len) {
  return crc32(crc, buf, len);
}
