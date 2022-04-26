// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_FORMATS_FORMAT_BASE_H_
#define SRC_MEDIA_VNEXT_LIB_FORMATS_FORMAT_BASE_H_

#include <fuchsia/mediastreams/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/media/vnext/lib/formats/compression.h"
#include "src/media/vnext/lib/formats/encryption.h"

namespace fmlib {

// Describes possible compression and possible encryption for an elementary stream.
class FormatBase {
 public:
  bool is_compressed() const { return !!compression_; }

  bool is_encrypted() const { return !!encryption_; }

  const Compression& compression() const {
    FX_CHECK(compression_);
    return *compression_;
  }

  const Encryption& encryption() const {
    FX_CHECK(encryption_);
    return *encryption_;
  }

  fuchsia::mediastreams::Compression fidl_compression() const {
    FX_CHECK(compression_);
    return compression_->fidl();
  }

  fuchsia::mediastreams::CompressionPtr fidl_compression_ptr() const {
    if (!compression_) {
      return nullptr;
    }

    return compression_->fidl_ptr();
  }

  fuchsia::mediastreams::Encryption fidl_encryption() const {
    FX_CHECK(encryption_);
    return encryption_->fidl();
  }

  fuchsia::mediastreams::EncryptionPtr fidl_encryption_ptr() const {
    if (!encryption_) {
      return nullptr;
    }

    return encryption_->fidl_ptr();
  }

 protected:
  FormatBase() = default;

  FormatBase(std::unique_ptr<Compression> compression, std::unique_ptr<Encryption> encryption)
      : compression_(std::move(compression)), encryption_(std::move(encryption)) {}

  FormatBase(fuchsia::mediastreams::CompressionPtr compression,
             fuchsia::mediastreams::EncryptionPtr encryption)
      : compression_(compression ? std::make_unique<Compression>(std::move(*compression))
                                 : nullptr),
        encryption_(encryption ? std::make_unique<Encryption>(std::move(*encryption)) : nullptr) {}

  FormatBase(const FormatBase& other)
      : compression_(ClonePtr(other.compression_)), encryption_(ClonePtr(other.encryption_)) {}

  FormatBase(FormatBase&& other) = default;

  FormatBase& operator=(FormatBase&& other) = default;

  FormatBase Clone() const { return FormatBase(ClonePtr(compression_), ClonePtr(encryption_)); }

  template <typename T>
  static std::unique_ptr<T> ClonePtr(const std::unique_ptr<T>& t) {
    return t ? std::make_unique<T>(t->Clone()) : nullptr;
  }

 private:
  std::unique_ptr<Compression> compression_;
  std::unique_ptr<Encryption> encryption_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_FORMATS_FORMAT_BASE_H_
