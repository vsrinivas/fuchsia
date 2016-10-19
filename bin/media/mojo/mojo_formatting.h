// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/interfaces/media_common.mojom.h"
#include "apps/media/interfaces/media_source.mojom.h"
#include "apps/media/interfaces/media_transport.mojom.h"
#include "apps/media/interfaces/media_types.mojom.h"
#include "apps/media/interfaces/timelines.mojom.h"
#include "apps/media/src/framework/formatting.h"
#include "mojo/public/interfaces/network/http_header.mojom.h"
#include "mojo/public/interfaces/network/network_error.mojom.h"
#include "mojo/public/interfaces/network/url_body.mojom.h"
#include "mojo/public/interfaces/network/url_request.mojom.h"
#include "mojo/public/interfaces/network/url_response.mojom.h"

namespace mojo {
namespace media {

// See services/media/framework/ostream.h for details.

// Mojo defines versions of operator<< for this that produce only numbers.
const char* StringFromMediaTypeMedium(MediaTypeMedium value);
const char* StringFromAudioSampleFormat(AudioSampleFormat value);

// The following overloads add newlines.

template <typename T>
std::ostream& operator<<(std::ostream& os, const InterfacePtr<T>& value);

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
std::ostream& operator<<(std::ostream& os,
                         const MediaSourceStreamDescriptorPtr& value);
std::ostream& operator<<(std::ostream& os, const TimelineTransformPtr& value);

std::ostream& operator<<(std::ostream& os, const HttpHeaderPtr& value);
std::ostream& operator<<(std::ostream& os, const URLBodyPtr& value);
std::ostream& operator<<(std::ostream& os, const URLRequestPtr& value);
std::ostream& operator<<(std::ostream& os, const URLResponsePtr& value);
std::ostream& operator<<(std::ostream& os, const NetworkErrorPtr& value);

template <typename T>
std::ostream& operator<<(std::ostream& os, const ScopedHandleBase<T>& value) {
  if (value.is_valid()) {
    return os << "<valid>";
  } else {
    return os << "<not valid>";
  }
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const Array<T>& value) {
  if (!value) {
    return os << "<nullptr>" << std::endl;
  } else if (value.size() == 0) {
    return os << "<empty>" << std::endl;
  } else {
    os << std::endl;
  }

  int index = 0;
  for (T& element : const_cast<Array<T>&>(value)) {
    os << begl << "[" << index++ << "] " << element;
  }

  return os;
}

}  // namespace media
}  // namespace mojo
