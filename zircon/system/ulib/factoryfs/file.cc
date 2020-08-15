// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "factoryfs/file.h"

namespace factoryfs {
File::File(fbl::RefPtr<Directory> root_dir, std::unique_ptr<DirectoryEntryManager> entry)
    : root_dir_(std::move(root_dir)), directory_entry_(std::move(entry)) {
  root_dir_->OpenFile(directory_entry_->GetName().data(), this);
}

uint32_t File::GetSize() const { return directory_entry_->GetDataSize(); }

std::string_view File::GetName() const { return directory_entry_->GetName(); }

zx_status_t File::InitFileVmo() {
  if (vmo_.is_valid()) {
    return ZX_OK;
  }

  zx_status_t status;
  const size_t vmo_size = fbl::round_up(GetSize(), kFactoryfsBlockSize);
  if ((status = zx::vmo::create(vmo_size, 0, &vmo_)) != ZX_OK) {
    FS_TRACE_ERROR("factoryfs: Failed to initialize vmo; error: %d\n", status);
    return status;
  }
  vmo_size_ = vmo_size;

  // TODO(manalib) append filename to make property-name unique for different files.
  zx_object_set_property(vmo_.get(), ZX_PROP_NAME, "factoryfs-file", strlen("factoryfs-file"));

  if ((status = root_dir_->Device().BlockAttachVmo(vmo_, &vmoid_)) != ZX_OK) {
    FS_TRACE_INFO("factoryfs:File::Failed to attach vmo to block device: %s\n",
                  zx_status_get_string(status));
    vmo_.reset();
    return status;
  }

  uint32_t dev_block_size = root_dir_->GetDeviceBlockInfo().block_size;
  uint32_t dev_blocks =
      fbl::round_up(directory_entry_->GetDataSize(), dev_block_size) / dev_block_size;
  // TODO manalib check if directory_entry_->GetDataSize() is within file size
  block_fifo_request_t request = {
      .opcode = BLOCKIO_READ,
      .vmoid = vmoid_.get(),
      .length = dev_blocks,
      .vmo_offset = 0,
      .dev_offset = FsToDeviceBlocks(directory_entry_->GetDataStart(), dev_block_size),
  };

  return root_dir_->Device().FifoTransaction(&request, 1);
}

zx_status_t File::Read(void* data, size_t len, size_t offset, size_t* out_actual) {
  if (data == nullptr || out_actual == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  // clip to EOF
  if (offset >= GetSize()) {
    *out_actual = 0;
    return ZX_OK;
  }
  if (len > (GetSize() - offset)) {
    len = GetSize() - offset;
  }

  zx_status_t status = ZX_OK;
  if ((status = InitFileVmo()) != ZX_OK) {
    FS_TRACE_ERROR("factoryfs: Failed to initialize VMO error:%s", zx_status_get_string(status));
    return status;
  }
  if ((status = vmo_.read(data, offset, len)) != ZX_OK) {
    FS_TRACE_ERROR("factoryfs: Failed to read VMO error:%s", zx_status_get_string(status));
    return status;
  }
  *out_actual = len;
  return ZX_OK;
}

zx_status_t File::Write(const void* data, size_t len, size_t offset, size_t* out_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t File::Truncate(size_t len) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t File::GetAttributes(fs::VnodeAttributes* attributes) {
  *attributes = fs::VnodeAttributes();
  attributes->mode = (V_TYPE_FILE | V_IRUSR);
  attributes->content_size = directory_entry_->GetDataSize();
  // There is no concept of inode number in factoryfs
  attributes->inode = ::llcpp::fuchsia::io::INO_UNKNOWN;
  attributes->storage_size = directory_entry_->GetDataSize();  // TODO(manalib) convert to blocks.
  attributes->link_count = 1;
  attributes->creation_time = 0;      // TODO(manalib)
  attributes->modification_time = 0;  // TODO(manalib)
  return ZX_OK;
}

File::~File() {
  root_dir_->CloseFile(GetName().data());
  root_dir_->Device().BlockDetachVmo(std::move(vmoid_));
}

zx_status_t File::Close() { return ZX_OK; }

}  // namespace factoryfs
