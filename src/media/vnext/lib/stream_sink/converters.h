// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_STREAM_SINK_CONVERTERS_H_
#define SRC_MEDIA_VNEXT_LIB_STREAM_SINK_CONVERTERS_H_

#include <fuchsia/media2/cpp/fidl.h>

#include "src/media/vnext/lib/stream_sink/release_fence.h"

namespace fmlib {

// Converter type for converting from internal packet type |T| to |fuchsia::media2::Packet|.
template <typename T>
struct ToPacketConverter {
  // Converts an internal packet of type |T| into a |fuchsia::media2::Packet|.
  static fuchsia::media2::Packet Convert(T& t);
};

// Converter type for converting from |fuchsia::media2::Packet| to internal packet type |T| using
// a context of type |U|.
template <typename T, typename U>
struct FromPacketConverter {
  // Converts a |fuchsia::media2::Packet| into an internal packet of type |T|. |release_fence|
  // should be deleted when the payload regions may be recycled. |context| provides any context
  // needed to do the conversion.
  static T Convert(fuchsia::media2::Packet packet, std::unique_ptr<ReleaseFence> release_fence,
                   U context);
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_STREAM_SINK_CONVERTERS_H_
