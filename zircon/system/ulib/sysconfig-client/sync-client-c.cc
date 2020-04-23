// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sysconfig/sync-client-c.h>
#include <lib/sysconfig/sync-client.h>

namespace {
sysconfig::SyncClient::PartitionType Translate(sysconfig_partition_t partition) {
  switch (partition) {
    case kSysconfigPartitionSysconfig:
      return sysconfig::SyncClient::PartitionType::kSysconfig;
    case kSysconfigPartitionABRMetadata:
      return sysconfig::SyncClient::PartitionType::kABRMetadata;
    case kSysconfigPartitionVerifiedBootMetadataA:
      return sysconfig::SyncClient::PartitionType::kVerifiedBootMetadataA;
    case kSysconfigPartitionVerifiedBootMetadataB:
      return sysconfig::SyncClient::PartitionType::kVerifiedBootMetadataB;
    case kSysconfigPartitionVerifiedBootMetadataR:
      return sysconfig::SyncClient::PartitionType::kVerifiedBootMetadataR;
    default:
      break;
  }
  ZX_ASSERT(false);
}
}  // namespace

struct sysconfig_sync_client {
  std::optional<sysconfig::SyncClient> cpp_client;
};

__EXPORT
zx_status_t sysconfig_sync_client_create(int devfs_root, sysconfig_sync_client_t** out_client) {
  auto client = std::make_unique<sysconfig_sync_client_t>();

  fbl::unique_fd fd(devfs_root);
  auto status = sysconfig::SyncClient::Create(fd, &client->cpp_client);
  fd.release();
  if (status != ZX_OK) {
    return status;
  }

  *out_client = client.release();
  return ZX_OK;
}

__EXPORT
void sysconfig_sync_client_free(sysconfig_sync_client_t* client) { delete client; }

__EXPORT
zx_status_t sysconfig_write_partition(sysconfig_sync_client_t* client,
                                      sysconfig_partition_t partition, zx_handle_t vmo,
                                      zx_off_t vmo_offset) {
  return client->cpp_client->WritePartition(Translate(partition), *zx::unowned_vmo(vmo),
                                            vmo_offset);
}

__EXPORT
zx_status_t sysconfig_read_partition(sysconfig_sync_client_t* client,
                                     sysconfig_partition_t partition, zx_handle_t vmo,
                                     zx_off_t vmo_offset) {
  return client->cpp_client->ReadPartition(Translate(partition), *zx::unowned_vmo(vmo), vmo_offset);
}

__EXPORT
zx_status_t sysconfig_get_partition_size(sysconfig_sync_client_t* client,
                                         sysconfig_partition_t partition, size_t* out) {
  return client->cpp_client->GetPartitionSize(Translate(partition), out);
}
