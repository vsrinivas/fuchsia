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
constexpr const char kFsName[] = "blobfs";

QueryService::QueryService(async_dispatcher_t* dispatcher, Blobfs* blobfs, Runner* runner)
    : fs::Service([dispatcher, this](fidl::ServerEnd<llcpp::fuchsia::fs::Query> server_end) {
        return fidl::BindSingleInFlightOnly(dispatcher, std::move(server_end), this);
      }),
      blobfs_(blobfs),
      runner_(runner) {}

void QueryService::GetInfo(FilesystemInfoQuery query, GetInfoCompleter::Sync& completer) {
  static_assert(fbl::constexpr_strlen(kFsName) < fuchsia_fs::wire::MAX_FS_NAME_LENGTH,
                "Blobfs name too long");

  fuchsia_fs::wire::FilesystemInfo::UnownedBuilder builder;

  uint64_t total_bytes;
  if (query & FilesystemInfoQuery::TOTAL_BYTES) {
    total_bytes = blobfs_->Info().data_block_count * blobfs_->Info().block_size;
    builder.set_total_bytes(fidl::unowned_ptr(&total_bytes));
  }

  uint64_t used_bytes;
  if (query & FilesystemInfoQuery::USED_BYTES) {
    used_bytes = blobfs_->Info().alloc_block_count * blobfs_->Info().block_size;
    builder.set_used_bytes(fidl::unowned_ptr(&used_bytes));
  }

  uint64_t total_nodes;
  if (query & FilesystemInfoQuery::TOTAL_NODES) {
    total_nodes = blobfs_->Info().inode_count;
    builder.set_total_nodes(fidl::unowned_ptr(&total_nodes));
  }

  uint64_t used_nodes;
  if (query & FilesystemInfoQuery::USED_NODES) {
    used_nodes = blobfs_->Info().alloc_inode_count;
    builder.set_used_nodes(fidl::unowned_ptr(&used_nodes));
  }

  zx::event fs_id;
  if (query & FilesystemInfoQuery::FS_ID) {
    zx_status_t status = blobfs_->GetFsId(&fs_id);
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
    builder.set_fs_id(fidl::unowned_ptr(&fs_id));
  }

  uint32_t block_size;
  if (query & FilesystemInfoQuery::BLOCK_SIZE) {
    block_size = kBlobfsBlockSize;
    builder.set_block_size(fidl::unowned_ptr(&block_size));
  }

  uint32_t max_node_name_size;
  if (query & FilesystemInfoQuery::MAX_NODE_NAME_SIZE) {
    max_node_name_size = digest::kSha256HexLength;
    builder.set_max_node_name_size(fidl::unowned_ptr(&max_node_name_size));
  }

  fuchsia_fs::wire::FsType fs_type;
  if (query & FilesystemInfoQuery::FS_TYPE) {
    fs_type = fuchsia_fs::wire::FsType::BLOBFS;
    builder.set_fs_type(fidl::unowned_ptr(&fs_type));
  }

  fidl::StringView name;
  if (query & FilesystemInfoQuery::NAME) {
    name = fidl::StringView(kFsName);
    builder.set_name(fidl::unowned_ptr(&name));
  }

  fidl::StringView device_path;
  char name_buf[llcpp::fuchsia::io2::wire::MAX_PATH_LENGTH];
  size_t name_len;
  if (query & FilesystemInfoQuery::DEVICE_PATH) {
    zx_status_t status = blobfs_->Device()->GetDevicePath(
        llcpp::fuchsia::io2::wire::MAX_PATH_LENGTH, name_buf, &name_len);
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
    // It appears that the |name_len| returned by |GetDevicePath| includes a trailing NUL.
    ZX_ASSERT(name_buf[name_len - 1] == '\0');
    device_path = fidl::StringView(name_buf, name_len - 1);
    builder.set_device_path(fidl::unowned_ptr(&device_path));
  }

  completer.ReplySuccess(builder.build());
}

void QueryService::IsNodeInFilesystem(zx::event token,
                                      IsNodeInFilesystemCompleter::Sync& completer) {
  completer.Reply(runner_->IsTokenAssociatedWithVnode(std::move(token)));
}

}  // namespace blobfs
