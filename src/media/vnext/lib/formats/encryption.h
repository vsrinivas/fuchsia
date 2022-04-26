// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_FORMATS_ENCRYPTION_H_
#define SRC_MEDIA_VNEXT_LIB_FORMATS_ENCRYPTION_H_

#include <fuchsia/mediastreams/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

namespace fmlib {

// Describes encryption applied to an elementary stream.
class Encryption {
 public:
  Encryption(std::string scheme, fidl::VectorPtr<uint8_t> default_key_id,
             fidl::VectorPtr<uint8_t> default_init_vector,
             std::unique_ptr<::fuchsia::mediastreams::EncryptionPattern> default_pattern);

  explicit Encryption(fuchsia::mediastreams::Encryption encryption)
      : fidl_(std::move(encryption)) {}

  Encryption(Encryption&& other) = default;

  Encryption& operator=(Encryption&& other) = default;

  operator fuchsia::mediastreams::Encryption() const { return fidl::Clone(fidl_); }

  Encryption Clone() const { return Encryption(fidl::Clone(fidl_)); }

  // Returns this encpryption as a |fuchsia::mediastreams::Encryption|.
  fuchsia::mediastreams::Encryption fidl() const { return fidl::Clone(fidl_); }

  // Returns this encpryption as a |fuchsia::mediastreams::EncryptionPtr|.
  fuchsia::mediastreams::EncryptionPtr fidl_ptr() const {
    return std::make_unique<fuchsia::mediastreams::Encryption>(fidl::Clone(fidl_));
  }

 private:
  fuchsia::mediastreams::Encryption fidl_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_FORMATS_ENCRYPTION_H_
