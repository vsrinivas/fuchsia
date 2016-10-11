// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/metadata.h"

namespace mojo {
namespace media {

// static
std::unique_ptr<Metadata> Metadata::Create(uint64_t duration_ns,
                                           const std::string& title,
                                           const std::string& artist,
                                           const std::string& album,
                                           const std::string& publisher,
                                           const std::string& genre,
                                           const std::string& composer) {
  return std::unique_ptr<Metadata>(new Metadata(
      duration_ns, title, artist, album, publisher, genre, composer));
}

Metadata::Metadata(uint64_t duration_ns,
                   const std::string& title,
                   const std::string& artist,
                   const std::string& album,
                   const std::string& publisher,
                   const std::string& genre,
                   const std::string& composer)
    : duration_ns_(duration_ns),
      title_(title),
      artist_(artist),
      album_(album),
      publisher_(publisher),
      genre_(genre),
      composer_(composer) {}

Metadata::~Metadata() {}

}  // namespace media
}  // namespace mojo
