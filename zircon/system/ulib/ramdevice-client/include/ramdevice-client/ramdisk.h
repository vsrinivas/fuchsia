// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RAMDEVICE_CLIENT_RAMDISK_H_
#define RAMDEVICE_CLIENT_RAMDISK_H_

#include <inttypes.h>
#include <stdlib.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

struct ramdisk_client;
typedef struct ramdisk_client ramdisk_client_t;

// Wait for a device at "path" to become available.
//
// Returns ZX_OK if the device is ready to be opened, or ZX_ERR_TIMED_OUT if
// the device is not available after "timeout" has elapsed.
zx_status_t wait_for_device(const char* path, zx_duration_t timeout);

// Wait for a device at |path| relative to |dirfd| to become available.
//
// Returns ZX_OK if the device is ready to be opened, or ZX_ERR_TIMED_OUT if
// the device is not available after "timeout" has elapsed.
zx_status_t wait_for_device_at(int dirfd, const char* path, zx_duration_t timeout);

// Creates a ramdisk and returns the full path to the ramdisk's block interface in ramdisk_path_out.
// This path should be at least PATH_MAX characters long.
zx_status_t ramdisk_create(uint64_t blk_size, uint64_t blk_count, ramdisk_client_t** out);
// Same as above except that it opens the ramdisk relative to the passed in 'dev_root_fd'.
// Ownership of 'dev_root_fd' is not transferred.
zx_status_t ramdisk_create_at(int dev_root_fd, uint64_t blk_size, uint64_t blk_count,
                              ramdisk_client_t** out);

// Creates a ramdisk and returns the full path to the ramdisk's block interface in ramdisk_path_out.
// This path should be at least PATH_MAX characters long.
zx_status_t ramdisk_create_with_guid(uint64_t blk_size, uint64_t blk_count,
                                     const uint8_t* type_guid, size_t guid_len,
                                     ramdisk_client_t** out);
// Same as above except that it opens the ramdisk relative to the passed in 'dev_root_fd'.
// Ownership of 'dev_root_fd' is not transferred.
zx_status_t ramdisk_create_at_with_guid(int dev_root_fd, uint64_t blk_size, uint64_t blk_count,
                                        const uint8_t* type_guid, size_t guid_len,
                                        ramdisk_client_t** out);

// Same but uses an existing VMO as the ramdisk.
// The handle is always consumed, and must be the only handle to this VMO.
zx_status_t ramdisk_create_from_vmo(zx_handle_t vmo, ramdisk_client_t** out);
// Same as above except that it opens the ramdisk relative to the passed in 'dev_root_fd'.
// Ownership of 'dev_root_fd' is not transferred.
zx_status_t ramdisk_create_at_from_vmo(int dev_root_fd, zx_handle_t vmo, ramdisk_client_t** out);
// Same as previous two, but with block size. If block_size is zero, a default block size is chosen.
zx_status_t ramdisk_create_from_vmo_with_block_size(zx_handle_t vmo, uint64_t block_size,
                                                    ramdisk_client_t** out);
zx_status_t ramdisk_create_at_from_vmo_with_block_size(int dev_root_fd, zx_handle_t vmo,
                                                       uint64_t block_size, ramdisk_client_t** out);

// Returns the file descriptor to the block device interface of the client.
//
// Does not transfer ownership of the file descriptor.
int ramdisk_get_block_fd(const ramdisk_client_t* client);

// Returns the path to the full block device interface of the ramdisk.
const char* ramdisk_get_path(const ramdisk_client_t* client);

// Puts the ramdisk at |ramdisk_path| to sleep after |blk_count| blocks written.
// After this, transactions will no longer be immediately persisted to disk.
// If the |RAMDISK_FLAG_RESUME_ON_WAKE| flag has been set, transactions will
// be processed when |ramdisk_wake| is called, otherwise they will fail immediately.
zx_status_t ramdisk_sleep_after(const ramdisk_client_t* client, uint64_t blk_count);

// Wake the ramdisk at |ramdisk_path| from a sleep state.
zx_status_t ramdisk_wake(const ramdisk_client_t* client);

// Grows the ramdisk up to |required_size|. |required_size| must be a multiple of
// the ramdisk block size and not less than the current size.
zx_status_t ramdisk_grow(const ramdisk_client_t* client, uint64_t required_size);

// A struct containing the number of write operations transmitted to the ramdisk
// since the last invocation of "wake" or "sleep_after".
typedef struct ramdisk_block_write_counts {
  uint64_t received;
  uint64_t successful;
  uint64_t failed;
} ramdisk_block_write_counts_t;

// Returns the ramdisk's current failed, successful, and total block counts as |counts|.
zx_status_t ramdisk_get_block_counts(const ramdisk_client_t* client,
                                     ramdisk_block_write_counts_t* out_counts);

// Sets flags on a ramdisk. Flags are plumbed directly through IPC interface.
zx_status_t ramdisk_set_flags(const ramdisk_client_t* client, uint32_t flags);

// Rebinds a ramdisk.
zx_status_t ramdisk_rebind(ramdisk_client_t* client);

// Destroys a ramdisk, given the "ramdisk_client" returned from "ramdisk_create".
zx_status_t ramdisk_destroy(ramdisk_client_t* client);

__END_CDECLS

#endif  // RAMDEVICE_CLIENT_RAMDISK_H_
