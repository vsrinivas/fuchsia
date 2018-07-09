// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_FORMATTING_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_FORMATTING_H_

#include <ostream>

#include "garnet/bin/media/media_player/framework/packet.h"
#include "garnet/bin/media/media_player/framework/result.h"
#include "garnet/bin/media/media_player/framework/stages/input.h"
#include "garnet/bin/media/media_player/framework/stages/output.h"
#include "garnet/bin/media/media_player/framework/stages/stage_impl.h"
#include "garnet/bin/media/media_player/framework/types/audio_stream_type.h"
#include "garnet/bin/media/media_player/framework/types/stream_type.h"
#include "garnet/bin/media/media_player/framework/types/video_stream_type.h"
#include "lib/fostr/indent.h"
#include "lib/media/timeline/timeline_function.h"

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

// The following overloads produce no fostr::NewLineines.

std::ostream& operator<<(std::ostream& os, Result value);
std::ostream& operator<<(std::ostream& os, const PacketPtr& value);
std::ostream& operator<<(std::ostream& os, StreamType::Medium value);
std::ostream& operator<<(std::ostream& os, AudioStreamType::SampleFormat value);
std::ostream& operator<<(std::ostream& os, VideoStreamType::VideoProfile value);
std::ostream& operator<<(std::ostream& os, VideoStreamType::PixelFormat value);
std::ostream& operator<<(std::ostream& os, VideoStreamType::ColorSpace value);
std::ostream& operator<<(std::ostream& os, const Bytes& value);
std::ostream& operator<<(std::ostream& os, media::TimelineRate value);
std::ostream& operator<<(std::ostream& os, media::TimelineFunction value);
std::ostream& operator<<(std::ostream& os, const GenericNode& value);
std::ostream& operator<<(std::ostream& os, const StageImpl& value);
std::ostream& operator<<(std::ostream& os, const Input& value);
std::ostream& operator<<(std::ostream& os, const Output& value);

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

// The following overloads can produce multiline output.

std::ostream& operator<<(std::ostream& os, const StreamType& value);
std::ostream& operator<<(std::ostream& os, const StreamTypeSet& value);

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

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_FORMATTING_H_
