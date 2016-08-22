// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FRAMEWORK_MOJO_MOJO_FORMATTING_H_
#define SERVICES_MEDIA_FRAMEWORK_MOJO_MOJO_FORMATTING_H_

#include "mojo/services/media/common/interfaces/media_common.mojom.h"
#include "mojo/services/media/common/interfaces/media_transport.mojom.h"
#include "mojo/services/media/common/interfaces/media_types.mojom.h"
#include "mojo/services/media/common/interfaces/timelines.mojom.h"
#include "mojo/services/media/control/interfaces/media_source.mojom.h"
#include "mojo/services/network/interfaces/network_service.mojom.h"
#include "services/media/framework/util/formatting.h"

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
std::ostream& operator<<(std::ostream& os, const URLRequestPtr& value);
std::ostream& operator<<(std::ostream& os, const URLResponsePtr& value);
std::ostream& operator<<(std::ostream& os, const NetworkErrorPtr& value);
std::ostream& operator<<(std::ostream& os,
                         const ScopedDataPipeConsumerHandle& value);

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

#endif  // SERVICES_MEDIA_FRAMEWORK_MOJO_MOJO_FORMATTING_H_
