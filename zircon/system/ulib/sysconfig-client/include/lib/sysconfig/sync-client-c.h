// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYSCONFIG_SYNC_CLIENT_C_H_
#define LIB_SYSCONFIG_SYNC_CLIENT_C_H_

#include <zircon/types.h>

__BEGIN_CDECLS

typedef struct sysconfig_sync_client sysconfig_sync_client_t;

typedef enum sysconfig_partition {
  kSysconfigPartitionSysconfig,
  // Used to determine which partition to boot into on boot.
  kSysconfigPartitionABRMetadata,
  // The follow are used to store verified boot metadata.
  kSysconfigPartitionVerifiedBootMetadataA,
  kSysconfigPartitionVerifiedBootMetadataB,
  kSysconfigPartitionVerifiedBootMetadataR,
} sysconfig_partition_t;

// Allocates and initializes a `sysconfig_sync_client_t`. This object should be passed into other
// APIs.
//
// Caller retains owernship of |devfs_root|.
zx_status_t sysconfig_sync_client_create(int devfs_root, sysconfig_sync_client_t** out_client);

// Frees the object created by `sysconfig_sync_client_create`.
void sysconfig_sync_client_free(sysconfig_sync_client_t* client);

// Provides write access for the partition specified. Always writes full partition.
//
// |vmo| must have a size greater than or equal to the partitions size + |vmo_offset|.
// Callee retains ownership of |vmo|.
zx_status_t sysconfig_write_partition(sysconfig_sync_client_t* client,
                                      sysconfig_partition_t partition, zx_handle_t vmo,
                                      zx_off_t vmo_offset);

// Provides read access for the partition specified. Always reads full partition.
//
// |vmo| must have a size greater than or equal to the partitions size + |vmo_offset|.
// Callee retains ownership of |vmo|.
zx_status_t sysconfig_read_partition(sysconfig_sync_client_t* client,
                                     sysconfig_partition_t partition, zx_handle_t vmo,
                                     zx_off_t vmo_offset);

// Returns the size of the partition specified.
zx_status_t sysconfig_get_partition_size(sysconfig_sync_client_t* client,
                                         sysconfig_partition_t partition, size_t* out);

__END_CDECLS

#endif  // LIB_SYSCONFIG_SYNC_CLIENT_C_H_
