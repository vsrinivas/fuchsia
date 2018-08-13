// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/framework/types/text_stream_type.h"

#include "garnet/bin/mediaplayer/util/safe_clone.h"
#include "lib/fxl/logging.h"

namespace media_player {

TextStreamType::TextStreamType(const std::string& encoding,
                               std::unique_ptr<Bytes> encoding_parameters)
    : StreamType(StreamType::Medium::kText, encoding,
                 std::move(encoding_parameters)) {}

TextStreamType::~TextStreamType() {}

const TextStreamType* TextStreamType::text() const { return this; }

std::unique_ptr<StreamType> TextStreamType::Clone() const {
  return Create(encoding(), SafeClone(encoding_parameters()));
}

TextStreamTypeSet::TextStreamTypeSet(const std::vector<std::string>& encodings)
    : StreamTypeSet(StreamType::Medium::kText, encodings) {}

TextStreamTypeSet::~TextStreamTypeSet() {}

const TextStreamTypeSet* TextStreamTypeSet::text() const { return this; }

std::unique_ptr<StreamTypeSet> TextStreamTypeSet::Clone() const {
  return Create(encodings());
}

}  // namespace media_player
