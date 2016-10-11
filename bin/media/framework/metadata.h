// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>

#include "apps/media/src/util/safe_clone.h"
#include "lib/ftl/macros.h"

namespace mojo {
namespace media {

class Metadata;

// Container for content metadata.
// TODO(dalesat): Probably needs to be extensible. Consider using map-like.
class Metadata {
 public:
  static std::unique_ptr<Metadata> Create(uint64_t duration_ns,
                                          const std::string& title,
                                          const std::string& artist,
                                          const std::string& album,
                                          const std::string& publisher,
                                          const std::string& genre,
                                          const std::string& composer);

  ~Metadata();

  uint64_t duration_ns() const { return duration_ns_; }

  const std::string& title() const { return title_; }

  const std::string& artist() const { return artist_; }

  const std::string& album() const { return album_; }

  const std::string& publisher() const { return publisher_; }

  const std::string& genre() const { return genre_; }

  const std::string& composer() const { return composer_; }

  std::unique_ptr<Metadata> Clone() const {
    return Create(duration_ns_, title_, artist_, album_, publisher_, genre_,
                  composer_);
  }

 private:
  Metadata(uint64_t duration_ns,
           const std::string& title,
           const std::string& artist,
           const std::string& album,
           const std::string& publisher,
           const std::string& genre,
           const std::string& composer);

  uint64_t duration_ns_;
  std::string title_;
  std::string artist_;
  std::string album_;
  std::string publisher_;
  std::string genre_;
  std::string composer_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Metadata);
};

}  // namespace media
}  // namespace mojo
