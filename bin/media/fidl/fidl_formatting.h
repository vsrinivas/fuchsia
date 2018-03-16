// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/handle.h>

#include <fuchsia/cpp/media.h>
#include <fuchsia/cpp/network.h>
#include "garnet/bin/media/framework/formatting.h"

namespace media {

// See services/media/framework/ostream.h for details.

// Fidl defines versions of operator<< for this that produce only numbers.
const char* StringFromMediaTypeMedium(MediaTypeMedium value);
const char* StringFromAudioSampleFormat(AudioSampleFormat value);

// The following overloads add newlines.

template <typename T>
std::ostream& operator<<(std::ostream& os, const fidl::InterfacePtr<T>& value);

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::unique_ptr<T>& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else {
    return os << *value;
  }
}

std::ostream& operator<<(std::ostream& os, const MediaType& value);
std::ostream& operator<<(std::ostream& os, const MediaTypeSet& value);
std::ostream& operator<<(std::ostream& os, const MediaTypeDetails& value);
std::ostream& operator<<(std::ostream& os, const MediaTypeSetDetails& value);
std::ostream& operator<<(std::ostream& os, const AudioMediaTypeDetails& value);
std::ostream& operator<<(std::ostream& os,
                         const AudioMediaTypeSetDetails& value);
std::ostream& operator<<(std::ostream& os, const VideoMediaTypeDetails& value);
std::ostream& operator<<(std::ostream& os,
                         const VideoMediaTypeSetDetails& value);
std::ostream& operator<<(std::ostream& os, const TextMediaTypeDetails& value);
std::ostream& operator<<(std::ostream& os,
                         const TextMediaTypeSetDetails& value);
std::ostream& operator<<(std::ostream& os,
                         const SubpictureMediaTypeDetails& value);
std::ostream& operator<<(std::ostream& os,
                         const SubpictureMediaTypeSetDetails& value);
std::ostream& operator<<(std::ostream& os, const TimelineTransform& value);

template <typename T>
std::ostream& operator<<(std::ostream& os, const zx::object<T>& value) {
  if (value) {
    return os << "<valid>";
  } else {
    return os << "<invalid>";
  }
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const fidl::VectorPtr<T>& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else if (value->size() == 0) {
    return os << "<empty>\n";
  } else {
    os << "\n";
  }

  int index = 0;
  for (const T& element : *value) {
    os << begl << "[" << index++ << "] " << element;
  }

  return os;
}

template <typename T>
struct AsInlineArray {
  explicit AsInlineArray(const fidl::VectorPtr<T>& value) : value_(value) {}
  const fidl::VectorPtr<T>& value_;
};

template <typename T>
std::ostream& operator<<(std::ostream& os, AsInlineArray<T> value) {
  if (!value.value_) {
    return os << "<nullptr>";
  } else if (value.value_->size() == 0) {
    return os << "<empty>";
  }

  for (const T& element : *value.value_) {
    os << element << ' ';
  }

  return os;
}

}  // namespace media
