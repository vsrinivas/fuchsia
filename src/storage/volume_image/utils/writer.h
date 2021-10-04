// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_UTILS_WRITER_H_
#define SRC_STORAGE_VOLUME_IMAGE_UTILS_WRITER_H_

#include <lib/fpromise/result.h>
#include <lib/stdcompat/span.h>

#include <string>

namespace storage::volume_image {

// Provides a reader interface to abstract platform, and devices particular nuisance to the image
// process.
//
// The reader requires an explicit offset, to allow compatibility with non posix interfaces, such as
// MTD.
class Writer {
 public:
  virtual ~Writer() = default;

  // On success data backing this writer is updated at [|offset|, |offset| +
  // |buffer.size()|] to |buffer|.
  //
  // On error the returned result to contains a string describing the error.
  virtual fpromise::result<void, std::string> Write(uint64_t offset,
                                                    cpp20::span<const uint8_t> buffer) = 0;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_UTILS_WRITER_H_
