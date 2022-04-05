// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BASE_DECRYPT_CONFIG_H_
#define MEDIA_BASE_DECRYPT_CONFIG_H_

#include <memory>
#include <string>
#include <string_view>

#include "media/base/subsample_entry.h"
namespace media {

const std::string kKeyValue = "key";
const std::string kInitializationVector = "iv";
const std::vector<SubsampleEntry> kSubsamples = {};

// We currently don't support decryption
class DecryptConfig {
 public:
  DecryptConfig() = default;
  ~DecryptConfig() = default;

  const std::string& key_id() const { return kKeyValue; }
  const std::string& iv() const { return kInitializationVector; }
  const std::vector<SubsampleEntry>& subsamples() const { return kSubsamples; }

  std::unique_ptr<DecryptConfig> Clone() const {
    return std::unique_ptr<DecryptConfig>();
  }

  std::unique_ptr<DecryptConfig> CopyNewSubsamplesIV(
      const std::vector<SubsampleEntry>&,
      const std::string&) {
    return std::unique_ptr<DecryptConfig>();
  }
};

}  // namespace media

#endif  // MEDIA_BASE_DECRYPT_CONFIG_H_
