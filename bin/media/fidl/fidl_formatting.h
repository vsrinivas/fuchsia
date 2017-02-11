// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>

#include "apps/media/services/media_result.fidl.h"
#include "apps/media/services/media_source.fidl.h"
#include "apps/media/services/media_transport.fidl.h"
#include "apps/media/services/media_types.fidl.h"
#include "apps/media/services/timelines.fidl.h"
#include "apps/media/src/framework/formatting.h"
#include "apps/network/services/http_header.fidl.h"
#include "apps/network/services/network_error.fidl.h"
#include "apps/network/services/url_body.fidl.h"
#include "apps/network/services/url_request.fidl.h"
#include "apps/network/services/url_response.fidl.h"

namespace media {

// See services/media/framework/ostream.h for details.

// Fidl defines versions of operator<< for this that produce only numbers.
const char* StringFromMediaTypeMedium(MediaTypeMedium value);
const char* StringFromAudioSampleFormat(AudioSampleFormat value);

// The following overloads add newlines.

template <typename T>
std::ostream& operator<<(std::ostream& os, const fidl::InterfacePtr<T>& value);

std::ostream& operator<<(std::ostream& os, const MediaTypePtr& value);
std::ostream& operator<<(std::ostream& os, const MediaTypeSetPtr& value);
std::ostream& operator<<(std::ostream& os, const MediaTypeDetailsPtr& value);
std::ostream& operator<<(std::ostream& os, const MediaTypeSetDetailsPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const AudioMediaTypeDetailsPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const AudioMediaTypeSetDetailsPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const VideoMediaTypeDetailsPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const VideoMediaTypeSetDetailsPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const TextMediaTypeDetailsPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const TextMediaTypeSetDetailsPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const SubpictureMediaTypeDetailsPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const SubpictureMediaTypeSetDetailsPtr& value);
std::ostream& operator<<(std::ostream& os, const TimelineTransformPtr& value);

std::ostream& operator<<(std::ostream& os, const network::HttpHeaderPtr& value);
std::ostream& operator<<(std::ostream& os, const network::URLBodyPtr& value);
std::ostream& operator<<(std::ostream& os, const network::URLRequestPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const network::URLResponsePtr& value);
std::ostream& operator<<(std::ostream& os,
                         const network::NetworkErrorPtr& value);

template <typename T>
std::ostream& operator<<(std::ostream& os, const mx::object<T>& value) {
  if (value) {
    return os << "<valid>";
  } else {
    return os << "<invalid>";
  }
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const fidl::Array<T>& value) {
  if (!value) {
    return os << "<nullptr>" << std::endl;
  } else if (value.size() == 0) {
    return os << "<empty>" << std::endl;
  } else {
    os << std::endl;
  }

  int index = 0;
  for (T& element : const_cast<fidl::Array<T>&>(value)) {
    os << begl << "[" << index++ << "] " << element;
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

}  // namespace media
