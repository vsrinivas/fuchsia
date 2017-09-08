// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "lib/media/timeline/timeline_rate.h"
#include "lib/media/fidl/media_result.fidl.h"
#include "lib/media/fidl/media_source.fidl.h"
#include "lib/media/fidl/media_transport.fidl.h"
#include "lib/media/fidl/media_types.fidl.h"
#include "lib/media/fidl/timelines.fidl.h"
#include "garnet/bin/flog_viewer/formatting.h"

namespace flog {
namespace handlers {

// Fidl defines versions of operator<< for this that produce only numbers.
const char* StringFromMediaTypeMedium(media::MediaTypeMedium value);
const char* StringFromAudioSampleFormat(media::AudioSampleFormat value);

// The following overloads add newlines.

template <typename T>
std::ostream& operator<<(std::ostream& os, const fidl::InterfacePtr<T>& value);

std::ostream& operator<<(std::ostream& os, const media::MediaTypePtr& value);
std::ostream& operator<<(std::ostream& os, const media::MediaTypeSetPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const media::MediaTypeDetailsPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const media::MediaTypeSetDetailsPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const media::AudioMediaTypeDetailsPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const media::AudioMediaTypeSetDetailsPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const media::VideoMediaTypeDetailsPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const media::VideoMediaTypeSetDetailsPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const media::TextMediaTypeDetailsPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const media::TextMediaTypeSetDetailsPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const media::SubpictureMediaTypeDetailsPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const media::SubpictureMediaTypeSetDetailsPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const media::TimelineTransformPtr& value);
std::ostream& operator<<(std::ostream& os, const media::MediaPacketPtr& value);
std::ostream& operator<<(std::ostream& os, const media::MediaPacket& value);
std::ostream& operator<<(std::ostream& os,
                         const media::MediaPacketDemandPtr& value);
std::ostream& operator<<(std::ostream& os, media::TimelineRate value);

struct AsNsTime {
  explicit AsNsTime(int64_t time) : time_(time) {}
  int64_t time_;
};

std::ostream& operator<<(std::ostream& os, AsNsTime value);

struct AsUsTime {
  explicit AsUsTime(int64_t time) : time_(time) {}
  int64_t time_;
};

std::ostream& operator<<(std::ostream& os, AsUsTime value);

struct AsMsTime {
  explicit AsMsTime(int64_t time) : time_(time) {}
  int64_t time_;
};

std::ostream& operator<<(std::ostream& os, AsMsTime value);

template <typename T>
std::ostream& operator<<(std::ostream& os, const fidl::Array<T>& value) {
  if (!value) {
    return os << "<nullptr>";
  } else if (value.size() == 0) {
    return os << "<empty>";
  }

  int index = 0;
  for (T& element : const_cast<fidl::Array<T>&>(value)) {
    os << "\n" << begl << "[" << index++ << "] " << element;
  }

  return os;
}

template <typename T>
struct AsInlineArray {
  explicit AsInlineArray(const fidl::Array<T>& value) : value_(value) {}
  const fidl::Array<T>& value_;
};

template <typename T>
std::ostream& operator<<(std::ostream& os, AsInlineArray<T> value) {
  if (!value.value_) {
    return os << "<nullptr>";
  } else if (value.value_.size() == 0) {
    return os << "<empty>";
  }

  for (T& element : const_cast<fidl::Array<T>&>(value.value_)) {
    os << element << ' ';
  }

  return os;
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& value) {
  if (value.empty()) {
    return os << "<empty>";
  }

  int index = 0;
  for (const T& element : value) {
    os << "\n" << begl << "[" << index++ << "] " << element;
  }

  return os;
}

}  // namespace handlers
}  // namespace flog
