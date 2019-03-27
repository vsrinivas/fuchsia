// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_FORMATTING_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_FORMATTING_H_

#include <ostream>
#include "lib/fostr/indent.h"
#include "lib/media/timeline/timeline_function.h"
#include "src/media/playback/mediaplayer_tmp/graph/nodes/input.h"
#include "src/media/playback/mediaplayer_tmp/graph/nodes/node.h"
#include "src/media/playback/mediaplayer_tmp/graph/nodes/output.h"
#include "src/media/playback/mediaplayer_tmp/graph/packet.h"
#include "src/media/playback/mediaplayer_tmp/graph/payloads/payload_config.h"
#include "src/media/playback/mediaplayer_tmp/graph/result.h"
#include "src/media/playback/mediaplayer_tmp/graph/types/audio_stream_type.h"
#include "src/media/playback/mediaplayer_tmp/graph/types/stream_type.h"
#include "src/media/playback/mediaplayer_tmp/graph/types/video_stream_type.h"

//
// This file declares a bunch of << operator overloads for dumping media stuff.
//
// Sufficiently short output is written with no leading or trailing whitespace
// or fostr::NewLineines. The caller should provide initial whitespace and
// terminating fostr::NewLineines as appropriate.
//
// Multiline output follows the same rules. Multiple lines will be output,
// usually with an initial fostr::NewLineine so the output starts on a new line.
// The last line of the output isn't terminated. Newlines in multiline output
// are padded on the left using the |begl| function, so the caller should set
// indentation (using fostr::Indent() and fostr::Outdent()) so that new lines
// are indented as desired.
//

namespace media_player {

std::ostream& operator<<(std::ostream& os, Result value);
std::ostream& operator<<(std::ostream& os, const PacketPtr& value);
std::ostream& operator<<(std::ostream& os, StreamType::Medium value);
std::ostream& operator<<(std::ostream& os, AudioStreamType::SampleFormat value);
std::ostream& operator<<(std::ostream& os, VideoStreamType::PixelFormat value);
std::ostream& operator<<(std::ostream& os, VideoStreamType::ColorSpace value);
std::ostream& operator<<(std::ostream& os, const Bytes& value);
std::ostream& operator<<(std::ostream& os, media::TimelineRate value);
std::ostream& operator<<(std::ostream& os, media::TimelineFunction value);
std::ostream& operator<<(std::ostream& os, const Node& value);
std::ostream& operator<<(std::ostream& os, const Input& value);
std::ostream& operator<<(std::ostream& os, const Output& value);
std::ostream& operator<<(std::ostream& os, const StreamType& value);
std::ostream& operator<<(std::ostream& os, const StreamTypeSet& value);
std::ostream& operator<<(std::ostream& os, PayloadMode value);
std::ostream& operator<<(std::ostream& os, VmoAllocation value);
std::ostream& operator<<(std::ostream& os, const PayloadConfig& value);
std::ostream& operator<<(std::ostream& os, const PayloadVmo& value);

template <typename T>
std::ostream& operator<<(std::ostream& os, Range<T> value) {
  return os << value.min << ".." << value.max;
}

// Time value in nanoseconds displayed as 0.123,456,789.
struct AsNs {
  AsNs(int64_t value) : value_(value) {}
  int64_t value_;
};

std::ostream& operator<<(std::ostream& os, AsNs value);

// Vector displayed in one line with spaces.
template <typename T>
struct AsInlineVector {
  explicit AsInlineVector(const std::vector<T>& value) : value_(value) {}
  const std::vector<T>& value_;
};

template <typename T>
std::ostream& operator<<(std::ostream& os, AsInlineVector<T> value) {
  if (value.value_.size() == 0) {
    return os << "<empty>";
  }

  for (T& element : const_cast<std::vector<T>&>(value.value_)) {
    os << element << ' ';
  }

  return os;
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::unique_ptr<T>& value) {
  if (!value) {
    return os << "<null>";
  }

  return os << *value;
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::shared_ptr<T>& value) {
  if (!value) {
    return os << "<null>";
  }

  return os << *value;
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& value) {
  if (value.size() == 0) {
    return os << "<empty>";
  }

  int index = 0;
  for (const T& element : value) {
    os << fostr::NewLine << "[" << index++ << "] " << element;
  }

  return os;
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const fbl::RefPtr<T>& value) {
  if (!value) {
    return os << "<null>";
  }

  return os << *value;
}

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_FORMATTING_H_
