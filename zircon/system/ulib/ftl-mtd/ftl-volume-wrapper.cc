// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ftl-mtd/ftl-volume-wrapper.h>

namespace ftl_mtd {

zx_status_t FtlVolumeWrapper::Init(std::unique_ptr<ftl::NdmDriver> driver) {
  if (volume_->Init(std::move(driver)) != nullptr) {
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

bool FtlVolumeWrapper::OnVolumeAdded(uint32_t page_size, uint32_t num_pages) {
  page_size_ = page_size;
  num_pages_ = num_pages;
  return true;
}

ssize_t FtlVolumeWrapper::Read(void* buffer, size_t count) {
  if (page_ >= num_pages_) {
    return 0;
  }

  if (count % page_size_ != 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t page_count = static_cast<uint32_t>(count / page_size_);
  zx_status_t status = volume_->Read(page_, page_count, buffer);
  if (status != ZX_OK) {
    return status;
  }

  page_ += page_count;
  return count;
}

ssize_t FtlVolumeWrapper::Write(const void* buffer, size_t count) {
  if (page_ >= num_pages_) {
    return 0;
  }

  if (count % page_size_ != 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t page_count = static_cast<uint32_t>(count / page_size_);
  zx_status_t status = volume_->Write(page_, page_count, buffer);
  if (status != ZX_OK) {
    return status;
  }

  page_ += page_count;
  return count;
}

ssize_t FtlVolumeWrapper::Seek(off_t offset, int whence) {
  if (offset % page_size_ != 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  off_t page_delta = static_cast<off_t>(offset / page_size_);
  off_t page;

  if (whence == SEEK_SET) {
    page = page_delta;
  } else if (whence == SEEK_END) {
    page = static_cast<off_t>(num_pages_) - page_delta;
  } else if (whence == SEEK_CUR) {
    page = static_cast<off_t>(page_) + page_delta;
  } else {
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t page_u32 = static_cast<uint32_t>(page);
  if (page != page_u32) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  page_ = page_u32;
  return Tell();
}

ssize_t FtlVolumeWrapper::Size() { return page_size_ * num_pages_; }

ssize_t FtlVolumeWrapper::Tell() { return page_size_ * page_; }

zx_status_t FtlVolumeWrapper::Truncate(size_t size) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t FtlVolumeWrapper::Sync() { return volume_->Flush(); }

zx_status_t FtlVolumeWrapper::Format() { return volume_->Format(); }

}  // namespace ftl_mtd
