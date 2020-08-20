// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/factory/factoryfs/query.h"

#include <lib/fidl-async/cpp/bind.h>

#include "src/storage/factory/factoryfs/factoryfs.h"
#include "src/storage/factory/factoryfs/format.h"
#include "src/storage/factory/factoryfs/runner.h"

namespace factoryfs {

namespace fuchsia_fs = ::llcpp::fuchsia::fs;

constexpr const char kFsName[] = "factoryfs";

QueryService::QueryService(async_dispatcher_t* dispatcher, Factoryfs* factoryfs, Runner* runner)
    : fs::Service([dispatcher, this](zx::channel server_end) {
        return fidl::BindSingleInFlightOnly(dispatcher, std::move(server_end), this);
      }),
      factoryfs_(factoryfs),
      runner_(runner) {}

void QueryService::GetInfo(fuchsia_fs::FilesystemInfoQuery query,
                           GetInfoCompleter::Sync completer) {
  static_assert(fbl::constexpr_strlen(kFsName) < fuchsia_fs::MAX_FS_NAME_LENGTH,
                "Factoryfs name too long");

  fuchsia_fs::FilesystemInfo::UnownedBuilder builder;

  uint64_t total_bytes;
  if (query & fuchsia_fs::FilesystemInfoQuery::TOTAL_BYTES) {
    // Account for 1 block for superblock.
    uint64_t num_blocks = 1 + factoryfs_->Info().data_blocks +
                          factoryfs_->Info().directory_ent_blocks;
    total_bytes = num_blocks * factoryfs_->Info().block_size;
    builder.set_total_bytes(fidl::unowned_ptr(&total_bytes));
  }

  uint64_t used_bytes;
  if (query & fuchsia_fs::FilesystemInfoQuery::USED_BYTES) {
    used_bytes = factoryfs_->Info().data_blocks * factoryfs_->Info().block_size;
    builder.set_used_bytes(fidl::unowned_ptr(&used_bytes));
  }

  uint64_t total_nodes;
  if (query & fuchsia_fs::FilesystemInfoQuery::TOTAL_NODES) {
    total_nodes = factoryfs_->Info().directory_entries;
    builder.set_total_nodes(fidl::unowned_ptr(&total_nodes));
  }

  uint64_t used_nodes;
  if (query & fuchsia_fs::FilesystemInfoQuery::USED_NODES) {
    used_nodes = factoryfs_->Info().directory_entries;
    builder.set_used_nodes(fidl::unowned_ptr(&used_nodes));
  }

  zx::event fs_id;
  if (query & fuchsia_fs::FilesystemInfoQuery::FS_ID) {
    zx_status_t status = factoryfs_->GetFsId(&fs_id);
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
    builder.set_fs_id(fidl::unowned_ptr(&fs_id));
  }

  uint32_t block_size;
  if (query & fuchsia_fs::FilesystemInfoQuery::BLOCK_SIZE) {
    block_size = kFactoryfsBlockSize;
    builder.set_block_size(fidl::unowned_ptr(&block_size));
  }

  uint32_t max_node_name_size;
  if (query & fuchsia_fs::FilesystemInfoQuery::MAX_NODE_NAME_SIZE) {
    max_node_name_size = kFactoryfsMaxNameSize;
    builder.set_max_node_name_size(fidl::unowned_ptr(&max_node_name_size));
  }

  fuchsia_fs::FsType fs_type;
  if (query & fuchsia_fs::FilesystemInfoQuery::FS_TYPE) {
    fs_type = fuchsia_fs::FsType::FACTORYFS;
    builder.set_fs_type(fidl::unowned_ptr(&fs_type));
  }

  fidl::StringView name;
  if (query & fuchsia_fs::FilesystemInfoQuery::NAME) {
    name = fidl::StringView(kFsName);
    builder.set_name(fidl::unowned_ptr(&name));
  }

  fidl::StringView device_path;
  char name_buf[llcpp::fuchsia::io2::MAX_PATH_LENGTH];
  size_t name_len;
  if (query & fuchsia_fs::FilesystemInfoQuery::DEVICE_PATH) {
    zx_status_t status = factoryfs_->Device().GetDevicePath(llcpp::fuchsia::io2::MAX_PATH_LENGTH,
                                                            name_buf, &name_len);
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
                                      IsNodeInFilesystemCompleter::Sync completer) {
  completer.Reply(runner_->IsTokenAssociatedWithVnode(std::move(token)));
}

}  // namespace factoryfs
