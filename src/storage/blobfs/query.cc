// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/query.h"

#include <fuchsia/fs/llcpp/fidl.h>
#include <lib/fidl-async/cpp/bind.h>

#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/runner.h"

using fuchsia_fs::wire::FilesystemInfoQuery;

namespace blobfs {

constexpr char kFsName[] = "blobfs";

QueryService::QueryService(async_dispatcher_t* dispatcher, Blobfs* blobfs, Runner* runner)
    : fs::Service([dispatcher, this](fidl::ServerEnd<fuchsia_fs::Query> server_end) {
        return fidl::BindSingleInFlightOnly(dispatcher, std::move(server_end), this);
      }),
      blobfs_(blobfs),
      runner_(runner) {}

void QueryService::GetInfo(FilesystemInfoQuery query, GetInfoCompleter::Sync& completer) {
  static_assert(sizeof(kFsName) < fuchsia_fs::wire::MAX_FS_NAME_LENGTH, "Blobfs name too long");

  fidl::FidlAllocator allocator;
  fuchsia_fs::wire::FilesystemInfo filesystem_info(allocator);

  if (query & FilesystemInfoQuery::TOTAL_BYTES) {
    filesystem_info.set_total_bytes(allocator,
                                    blobfs_->Info().data_block_count * blobfs_->Info().block_size);
  }

  if (query & FilesystemInfoQuery::USED_BYTES) {
    filesystem_info.set_used_bytes(allocator,
                                   blobfs_->Info().alloc_block_count * blobfs_->Info().block_size);
  }

  if (query & FilesystemInfoQuery::TOTAL_NODES) {
    filesystem_info.set_total_nodes(allocator, blobfs_->Info().inode_count);
  }

  if (query & FilesystemInfoQuery::USED_NODES) {
    filesystem_info.set_used_nodes(allocator, blobfs_->Info().alloc_inode_count);
  }

  if (query & FilesystemInfoQuery::FS_ID) {
    zx::event fs_id;
    zx_status_t status = blobfs_->GetFsId(&fs_id);
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
    filesystem_info.set_fs_id(allocator, std::move(fs_id));
  }

  if (query & FilesystemInfoQuery::BLOCK_SIZE) {
    filesystem_info.set_block_size(allocator, kBlobfsBlockSize);
  }

  if (query & FilesystemInfoQuery::MAX_NODE_NAME_SIZE) {
    filesystem_info.set_max_node_name_size(allocator, digest::kSha256HexLength);
  }

  if (query & FilesystemInfoQuery::FS_TYPE) {
    filesystem_info.set_fs_type(allocator, fuchsia_fs::wire::FsType::BLOBFS);
  }

  if (query & FilesystemInfoQuery::NAME) {
    fidl::StringView name(kFsName);
    filesystem_info.set_name(allocator, std::move(name));
  }

  char name_buf[fuchsia_io2::wire::MAX_PATH_LENGTH];
  if (query & FilesystemInfoQuery::DEVICE_PATH) {
    size_t name_len;
    zx_status_t status =
        blobfs_->Device()->GetDevicePath(fuchsia_io2::wire::MAX_PATH_LENGTH, name_buf, &name_len);
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
    // It appears that the |name_len| returned by |GetDevicePath| includes a trailing NUL.
    ZX_ASSERT(name_buf[name_len - 1] == '\0');
    fidl::StringView device_path(name_buf, name_len - 1);
    filesystem_info.set_device_path(allocator, std::move(device_path));
  }

  completer.ReplySuccess(std::move(filesystem_info));
}

void QueryService::IsNodeInFilesystem(zx::event token,
                                      IsNodeInFilesystemCompleter::Sync& completer) {
  completer.Reply(runner_->IsTokenAssociatedWithVnode(std::move(token)));
}

}  // namespace blobfs
