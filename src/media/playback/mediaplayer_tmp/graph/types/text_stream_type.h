// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_TYPES_TEXT_STREAM_TYPE_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_TYPES_TEXT_STREAM_TYPE_H_

#include <memory>

#include "src/media/playback/mediaplayer_tmp/graph/types/stream_type.h"

namespace media_player {

// Describes the type of a text stream.
class TextStreamType : public StreamType {
 public:
  static std::unique_ptr<StreamType> Create(
      const std::string& encoding, std::unique_ptr<Bytes> encoding_parameters) {
    return std::unique_ptr<StreamType>(
        new TextStreamType(encoding, std::move(encoding_parameters)));
  }

  TextStreamType(const std::string& encoding,
                 std::unique_ptr<Bytes> encoding_parameters);

  ~TextStreamType() override;

  const TextStreamType* text() const override;

  std::unique_ptr<StreamType> Clone() const override;
};

// Describes a set of text stream types.
class TextStreamTypeSet : public StreamTypeSet {
 public:
  static std::unique_ptr<StreamTypeSet> Create(
      const std::vector<std::string>& encodings) {
    return std::unique_ptr<StreamTypeSet>(new TextStreamTypeSet(encodings));
  }

  TextStreamTypeSet(const std::vector<std::string>& encodings);

  ~TextStreamTypeSet() override;

  const TextStreamTypeSet* text() const override;

  std::unique_ptr<StreamTypeSet> Clone() const override;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_TYPES_TEXT_STREAM_TYPE_H_
