// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYSCONFIG_SYNC_CLIENT_H_
#define LIB_SYSCONFIG_SYNC_CLIENT_H_

#include <fuchsia/hardware/skipblock/llcpp/fidl.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <optional>

#include <fbl/unique_fd.h>

#include "abr-wear-leveling.h"
#include "sysconfig-header.h"

namespace sysconfig {

namespace skipblock = ::llcpp::fuchsia::hardware::skipblock;

// This class provides a synchronous read and write interface into sub-partitions of the sysconfig
// skip-block partition.
//
// The class takes into account differences that may appear in partition layout between various
// device's sysconfig partitions.
class __EXPORT SyncClient {
 public:
  // The sub partitions of the sysconfig partition.
  enum class PartitionType {
    kSysconfig,
    // Used to determine which partition to boot into on boot.
    kABRMetadata,
    // The follow are used to store verified boot metadata.
    kVerifiedBootMetadataA,
    kVerifiedBootMetadataB,
    kVerifiedBootMetadataR,
  };

  // Looks for a skip-block device of type sysconfig. If found, returns a client capable of reading
  // and writing to sub-partitions of the sysconfig device.
  static zx_status_t Create(std::optional<SyncClient>* out);

  // Variation on `Create` with devfs (/dev) injected.
  static zx_status_t Create(const fbl::unique_fd& devfs_root, std::optional<SyncClient>* out);

  // Provides write access for the partition specified. Always writes full partition.
  //
  // |vmo| must have a size greater than or equal to the partitions size + |vmo_offset|.
  zx_status_t WritePartition(PartitionType partition, const zx::vmo& vmo, zx_off_t vmo_offset);

  // Write pages without first erasing.
  zx_status_t WriteBytesWithoutErase(size_t offset, size_t len, const zx::vmo& vmo,
                                     zx_off_t vmo_offset);

  // Provides read access for the partition specified. Always reads full partition.
  //
  // |vmo| must have a size greater than or equal to the partitions size + |vmo_offset|.
  zx_status_t ReadPartition(PartitionType partition, const zx::vmo& vmo, zx_off_t vmo_offset);

  // Returns the size of the partition specified.
  zx_status_t GetPartitionSize(PartitionType partition, size_t* out);

  zx_status_t GetPartitionOffset(PartitionType partition, size_t* out);

  // Use caution when updating layout in multi-threaded context.
  // It's dangerous to update layout and header while there are other instances of SyncClient
  // in use.
  // In particular, SyncClient caches header from storage after the first time it reads it. If
  // layout is changed afterwards by some other instance of SyncClient, it will not be aware of it.
  // Thus make sure that you only effectively update layout in a state where no other SyncClient is
  // created and in use.
  zx_status_t UpdateLayout(const sysconfig_header& target_header);

  // No copy.
  SyncClient(const SyncClient&) = delete;
  SyncClient& operator=(const SyncClient&) = delete;

  SyncClient(SyncClient&&) = default;
  SyncClient& operator=(SyncClient&&) = default;
  // TODO(fxbug.dev/47505): Swap the return and output argument.
  const sysconfig_header* GetHeader(zx_status_t* status_out = nullptr);

 private:
  SyncClient(::llcpp::fuchsia::hardware::skipblock::SkipBlock::SyncClient skip_block)
      : skip_block_(std::move(skip_block)) {}

  zx_status_t InitializeReadMapper();

  zx_status_t Write(
      size_t offset, size_t len, const zx::vmo& vmo, zx_off_t vmo_offset,
      skipblock::WriteBytesMode mode = skipblock::WriteBytesMode::READ_MODIFY_ERASE_WRITE);

  zx_status_t Read(size_t offset, size_t len, const zx::vmo& vmo, zx_off_t vmo_offset);

  zx_status_t LoadFromStorage();

  ::llcpp::fuchsia::hardware::skipblock::SkipBlock::SyncClient skip_block_;

  // Lazily initialized on reads.
  fzl::OwnedVmoMapper read_mapper_;

  // Once loaded from storage, the header will be cached here
  std::unique_ptr<sysconfig_header> header_;

  // The friend declaration here is mainly for allowing SynClientBuffered to re-use internal
  // resources such as read_mapper_, without having to expose them, change access modifier or
  // create another one. It makes possible the use of class composition method instead of
  // inheritance, which is usually preferred.
  friend class SyncClientBuffered;
  friend class SyncClientAbrWearLeveling;
};

// SynClientBuffered is a wrapper of SyncClient added with write-caching capability.
// It buffers all the write to sysconfig partition to an internal buffer first.
// The bufferred data is written to persistant storage by explicitly calling its Flush() method.
class SyncClientBuffered {
 public:
  using PartitionType = SyncClient::PartitionType;

  SyncClientBuffered(::sysconfig::SyncClient client) : client_(std::move(client)) {}

  virtual ~SyncClientBuffered() = default;

  // Similar public interfaces as synclient

  // The following can be re-implemented by child class.
  virtual zx_status_t WritePartition(PartitionType partition, const zx::vmo& vmo,
                                     zx_off_t vmo_offset);
  virtual zx_status_t ReadPartition(PartitionType partition, const zx::vmo& vmo,
                                    zx_off_t vmo_offset);
  virtual zx_status_t Flush();

  zx_status_t GetPartitionSize(PartitionType partition, size_t* size);
  zx_status_t GetPartitionOffset(PartitionType partition, size_t* size);

  // used for test
  const uint8_t* GetCacheBuffer(PartitionType partition);

  // No copy.
  SyncClientBuffered(const SyncClientBuffered&) = delete;
  SyncClientBuffered& operator=(const SyncClientBuffered&) = delete;

  SyncClientBuffered(SyncClientBuffered&&) = default;
  SyncClientBuffered& operator=(SyncClientBuffered&&) = default;

  zx_status_t UpdateLayout(const sysconfig_header& target_header);

 protected:
  enum CacheBitMask {
    kSysconfig = 1 << 0,
    kAbrMetadata = 1 << 1,
    kVbmetaA = 1 << 2,
    kVbmetaB = 1 << 3,
    kVbmetaR = 1 << 4
  };

  // A bit mask indicating whether the cache is empty
  // 0 means empty, 1 means non-empty
  uint32_t cache_modified_flag_ = 0;

  fzl::OwnedVmoMapper cache_;

  SyncClient client_;

  zx_status_t CreateCache();
  void InvalidateCache();
  void MarkCacheNonEmpty(PartitionType partition);
  bool IsCacheEmpty(PartitionType partition);
  bool IsAllCacheEmpty();
  zx_status_t WriteCache(PartitionType partition, const zx::vmo& vmo, zx_off_t vmo_offset);
  zx_status_t ReadCache(PartitionType partition, const zx::vmo& vmo, zx_off_t vmo_offset);
  uint32_t PartitionTypeToCacheMask(PartitionType partition);
  zx_status_t GetSubpartitionCacheAddrSize(PartitionType partition, uint8_t** start, size_t* size);
};

/**
 * Specialized sysconfig client for astro with NAND I/O optimization
 * Implement buffered write + abr wear-leveling
 */
class SyncClientAbrWearLeveling : public SyncClientBuffered {
 public:
  using PartitionType = SyncClient::PartitionType;

  SyncClientAbrWearLeveling(::sysconfig::SyncClient client)
      : SyncClientBuffered(std::move(client)), erase_count_(0) {}

  // No copy.
  SyncClientAbrWearLeveling(const SyncClientAbrWearLeveling&) = delete;
  SyncClientAbrWearLeveling& operator=(const SyncClientAbrWearLeveling&) = delete;

  SyncClientAbrWearLeveling(SyncClientAbrWearLeveling&&) = default;
  SyncClientAbrWearLeveling& operator=(SyncClientAbrWearLeveling&&) = default;

  zx_status_t ReadPartition(PartitionType partition, const zx::vmo& vmo,
                            zx_off_t vmo_offset) override;

  zx_status_t Flush() override;

  static const sysconfig_header& GetAbrWearLevelingSupportedLayout();

  // For test purpose
  uint32_t GetEraseCount() const { return erase_count_; }

  zx_status_t ValidateAbrMetadataInStorage(const abr_metadata_ext* expected);

 private:
  bool IsOnlyAbrMetadataModified();
  zx_status_t ReadLatestAbrMetadataFromStorage(const zx::vmo& vmo, zx_off_t vmo_offset);
  zx_status_t FindLatestAbrMetadataFromStorage(abr_metadata_ext* out);
  zx_status_t FlushAppendAbrMetadata(const sysconfig_header* header);
  zx_status_t FlushReset(const sysconfig_header* header);
  // For test purpose
  uint32_t erase_count_;
};

}  // namespace sysconfig

#endif  // LIB_SYSCONFIG_SYNC_CLIENT_H_
