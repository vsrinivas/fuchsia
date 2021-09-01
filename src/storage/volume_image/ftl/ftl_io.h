// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_FTL_FTL_IO_H_
#define SRC_STORAGE_VOLUME_IMAGE_FTL_FTL_IO_H_

#include <lib/fpromise/result.h>
#include <lib/ftl/ndm-driver.h>
#include <lib/ftl/volume.h>

#include <cstdint>

#include "src/storage/volume_image/utils/reader.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {

// Required construct for the FTL.
class FtlInstance final : public ftl::FtlInstance {
 public:
  bool OnVolumeAdded(uint32_t page_size, uint32_t num_pages) final {
    page_count_ = num_pages;
    page_size_ = page_size;

    return true;
  }

  uint64_t page_count() const { return page_count_; }
  uint64_t page_size() const { return page_size_; }

 private:
  uint64_t page_count_ = 0;
  uint64_t page_size_ = 0;
};

// Provides ownership of the FTL volume and the instance of the FTL.
//
// Any generated |Reader| or |Writer| will prolong the lifetime of the underlying handle.
// That is, it is safe to continue to use reader and writers generated from a handle instance,
// even if the last reference to the handle goes away, since the reader and writer, hold references
// to the internal objects as well.
class FtlHandle {
 public:
  FtlHandle()
      : instance_(std::make_unique<FtlInstance>()),
        volume_(std::make_unique<ftl::VolumeImpl>(instance_.get())) {}

  fpromise::result<void, std::string> Init(std::unique_ptr<ftl::NdmDriver> driver);

  ftl::Volume& volume() { return *volume_; }

  FtlInstance& instance() { return *instance_; }

  // Returns a reader instance that reads from the FTL volume.
  std::unique_ptr<Reader> MakeReader();

  // Returns a writer instance that writes into the FTL volume.
  std::unique_ptr<Writer> MakeWriter();

 private:
  // Each reader/writer instance would keep a reference to this.
  std::shared_ptr<FtlInstance> instance_;
  std::shared_ptr<ftl::Volume> volume_;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_FTL_FTL_IO_H_
