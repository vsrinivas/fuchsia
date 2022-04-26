// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_FORMATS_COMPRESSION_H_
#define SRC_MEDIA_VNEXT_LIB_FORMATS_COMPRESSION_H_

#include <fuchsia/mediastreams/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

namespace fmlib {

// Describes compression applied to an elementary stream.
class Compression {
 public:
  Compression(std::string type, fidl::VectorPtr<uint8_t> parameters);

  explicit Compression(fuchsia::mediastreams::Compression compression);

  Compression(Compression&& other) = default;

  operator fuchsia::mediastreams::Compression() const;

  Compression Clone() const { return Compression(fidl_); }

  // Returns this compression as a |fuchsia::mediastreams::Compression|.
  fuchsia::mediastreams::Compression fidl() const { return fidl_; }

  // Returns this compression as a |fuchsia::mediastreams::CompressionPtr|.
  fuchsia::mediastreams::CompressionPtr fidl_ptr() const {
    return std::make_unique<fuchsia::mediastreams::Compression>(fidl_);
  }

  std::string type() const { return fidl_.type; }

  const fidl::VectorPtr<uint8_t>& parameters() const { return fidl_.parameters; }

 private:
  fuchsia::mediastreams::Compression fidl_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_FORMATS_COMPRESSION_H_
