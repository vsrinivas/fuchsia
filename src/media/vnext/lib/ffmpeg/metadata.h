// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_FFMPEG_METADATA_H_
#define SRC_MEDIA_VNEXT_LIB_FFMPEG_METADATA_H_

#include <fuchsia/audiovideo/cpp/fidl.h>
extern "C" {
#include "libavformat/avformat.h"
}

#include <unordered_map>

namespace fmlib {

class Metadata {
 public:
  Metadata() = default;

  explicit Metadata(const fuchsia::audiovideo::Metadata& fidl);

  explicit Metadata(AVDictionary* source) { Merge(source); }

  Metadata(Metadata&& other) = default;

  Metadata& operator=(Metadata&& other) = default;

  bool empty() const { return values_by_label_.empty(); }

  fuchsia::audiovideo::Metadata fidl() const;
  operator fuchsia::audiovideo::Metadata() const { return fidl(); }

  fuchsia::audiovideo::MetadataPtr fidl_ptr() const;

  void Merge(AVDictionary* source);

 private:
  std::unordered_map<std::string, std::string> values_by_label_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_FFMPEG_METADATA_H_
