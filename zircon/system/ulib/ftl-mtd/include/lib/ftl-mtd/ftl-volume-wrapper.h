// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FTL_MTD_FTL_VOLUME_WRAPPER_H_
#define FTL_MTD_FTL_VOLUME_WRAPPER_H_

#include <lib/ftl/volume.h>
#include <sys/types.h>

#include <memory>

#include <fvm-host/file-wrapper.h>

namespace ftl_mtd {

class FtlVolumeWrapper : public ftl::FtlInstance, public fvm::host::FileWrapper {
 public:
  // Contructs an FtlVolumeWrapper.
  FtlVolumeWrapper() : volume_(new ftl::VolumeImpl(this)) {}

  // Contructs an FtlVolumeWrapper with the given |volume| instance.
  // Used for testing.
  explicit FtlVolumeWrapper(std::unique_ptr<ftl::Volume> volume) : volume_(std::move(volume)) {}

  // Initializes the FtlVolumeWrapper. Must be called before any operation is performed
  // with the FtlVolumeWrapper.
  zx_status_t Init(std::unique_ptr<ftl::NdmDriver> driver);

  // Formats the FTL volume (erases all data).
  zx_status_t Format();

  // FtlInstance interface:
  bool OnVolumeAdded(uint32_t page_size, uint32_t num_pages) override;

  // FileWrapper interface:
  ssize_t Read(void* buffer, size_t count) override;
  ssize_t Write(const void* buffer, size_t count) override;
  ssize_t Seek(off_t offset, int whence) override;
  ssize_t Size() override;
  ssize_t Tell() override;
  zx_status_t Truncate(size_t size) override;
  zx_status_t Sync() override;

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(FtlVolumeWrapper);

 private:
  std::unique_ptr<ftl::Volume> volume_;
  uint32_t page_ = 0;
  uint32_t page_size_ = 0;
  uint32_t num_pages_ = 0;
};

}  // namespace ftl_mtd

#endif  // FTL_MTD_FTL_VOLUME_WRAPPER_H_
