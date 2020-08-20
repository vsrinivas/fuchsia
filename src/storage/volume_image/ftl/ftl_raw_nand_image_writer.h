// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_FTL_FTL_RAW_NAND_IMAGE_WRITER_H_
#define SRC_STORAGE_VOLUME_IMAGE_FTL_FTL_RAW_NAND_IMAGE_WRITER_H_

#include <lib/fit/result.h>

#include <cstdint>
#include <tuple>

#include <fbl/span.h>

#include "src/storage/volume_image/ftl/options.h"
#include "src/storage/volume_image/ftl/raw_nand_image.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {

// This writer provdes an adapter layer between the expected hardware page and oob size, and the
// minimum requirements for the FTL.
//
// The FTL requires a minimum number of oob bytes, if this is not met by the underlying hardware,
// pages are merged, so that the oob bytes of two consecutive pages can be treated a single one.
//
// The end result is that pages and oob size get multiplied by a factor such that min(k) such that
// k * hardware_oob_size >= min FTL oob size.
class FtlRawNandImageWriter final : public Writer {
 public:
  // Returns a |FtlRawNandWriter| that will translate requests from the |device_options| into
  // the returned |options| that are guaranteed to be valid for FTL metadata on success.
  static fit::result<std::tuple<FtlRawNandImageWriter, RawNandOptions>, std::string> Create(
      const RawNandOptions& device_options, fbl::Span<const RawNandImageFlag> flags,
      ImageFormat format, Writer* writer);

  FtlRawNandImageWriter() = delete;
  FtlRawNandImageWriter(const FtlRawNandImageWriter&) = delete;
  FtlRawNandImageWriter(FtlRawNandImageWriter&&) = default;
  FtlRawNandImageWriter& operator=(const FtlRawNandImageWriter&) = delete;
  FtlRawNandImageWriter& operator=(FtlRawNandImageWriter&&) = delete;

  // On success data backing this writer is updated at [|offset|, |offset| +
  // |buffer.size()|] to |buffer|.
  //
  // This Write method expects page data and page oob to be performed in separate calls.
  //
  // On error the returned result to contains a string describing the error.
  fit::result<void, std::string> Write(uint64_t offset, fbl::Span<const uint8_t> data) final;

  // Returns a scalar describing how pages are coalesced to meet the upper layer requirements.
  constexpr int scale_factor() const { return scale_factor_; }

 private:
  explicit FtlRawNandImageWriter(const RawNandOptions& device_options, int scale_factor,
                                 Writer* writer)
      : options_(device_options), scale_factor_(scale_factor), writer_(writer) {}

  // Options for the hardware being written.
  RawNandOptions options_;

  // Represents how pages are merged together to meet the minimum number of OOB Bytes required for
  // the FTL.
  int scale_factor_ = 1;

  // Wrapped writer.
  Writer* writer_ = nullptr;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_FTL_FTL_RAW_NAND_IMAGE_WRITER_H_
