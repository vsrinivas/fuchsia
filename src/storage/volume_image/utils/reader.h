// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_UTILS_READER_H_
#define SRC_STORAGE_VOLUME_IMAGE_UTILS_READER_H_

#include <lib/fpromise/result.h>
#include <lib/stdcompat/span.h>

#include <limits>
#include <string>

namespace storage::volume_image {

// Provides a reader interface to abstract platform, and devices particular nuisance to the image
// process.
//
// The reader requires an explicit offset, to allow compatibility with non posix interfaces, such as
// MTD.
class Reader {
 public:
  virtual ~Reader() = default;

  // Returns the number of bytes readable from this reader.
  virtual uint64_t length() const = 0;

  // On success data at [|offset|, |offset| + |buffer.size()|] are read into
  // |buffer|.
  //
  // On error the returned result to contains a string describing the error.
  virtual fpromise::result<void, std::string> Read(uint64_t offset,
                                                   cpp20::span<uint8_t> buffer) const = 0;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_UTILS_READER_H_
