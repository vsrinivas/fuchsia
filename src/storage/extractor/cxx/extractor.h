// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_EXTRACTOR_CXX_EXTRACTOR_H_
#define SRC_STORAGE_EXTRACTOR_CXX_EXTRACTOR_H_

#include <lib/fit/result.h>
#include <lib/zx/status.h>
#include <zircon/errors.h>

#include <cstdint>
#include <memory>

#include <fbl/unique_fd.h>

#include "src/storage/extractor/c/extractor.h"

namespace extractor {

class Extractor {
 public:
  // Creates a new Extractor instance.
  // |input_stream|: Open fd to the storage that needs to be extracted.
  // |options|: ExtractorOptions.
  // |output_stream|: Open fd of the image "stream" where extracted data will be written.
  static zx::status<std::unique_ptr<Extractor>> Create(fbl::unique_fd input_stream,
                                                       ExtractorOptions options,
                                                       fbl::unique_fd output_stream);

  ~Extractor() { extractor_delete(extractor_); }

  // Adds an extent of properties |properties| that starts at |offset| having size as |size|.
  // |offset| and |size| need to be aligned to ExtractorOptions.alignment.
  zx::status<> Add(uint64_t offset, uint64_t size, ExtentProperties properties);

  // A helper routine that adds |block_count| blocks - where each "block" is of size
  // ExtractorOptions.alignment.
  zx::status<> AddBlocks(uint64_t block_offset, uint64_t block_count, ExtentProperties properties);

  // A helper routine that adds one block of size ExtractorOptions.alignment.
  zx::status<> AddBlock(uint64_t block_offset, ExtentProperties properties);

  // Writes the extractor data to the image file.
  zx::status<> Write();

 private:
  Extractor() = default;
  Extractor(const Extractor&) = delete;
  Extractor& operator=(const Extractor&) = delete;
  Extractor(Extractor&&) = delete;
  Extractor& operator=(Extractor&&) = delete;

  // Open fd of the input storage file.
  fbl::unique_fd input_stream_;

  // Open fd of the output extracted image.
  fbl::unique_fd output_stream_;

  // Extractor options.
  ExtractorOptions options_;

  // Underlying rust extractor.
  ExtractorRust* extractor_ = nullptr;
};

// Extract minfs filesystem contained in |input_fd|.
zx::status<> MinfsExtract(fbl::unique_fd input_fd, extractor::Extractor& extractor);

}  // namespace extractor

#endif  // SRC_STORAGE_EXTRACTOR_CXX_EXTRACTOR_H_
