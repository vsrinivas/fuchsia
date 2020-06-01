// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FORMATTING_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FORMATTING_H_

#include <fuchsia/sysmem/cpp/fidl.h>

namespace media_player {
namespace test {

// These should be defined in media_player::test rather than fuchsia::sysmem, because of
// conflicts with fostr/fidl definitions.

// In constrast to fostr/fidl, these format values as C++ code for goldens.

std::ostream& operator<<(std::ostream& os, const fuchsia::sysmem::BufferUsage& value);

std::ostream& operator<<(std::ostream& os, fuchsia::sysmem::HeapType value);

std::ostream& operator<<(std::ostream& os, const fuchsia::sysmem::BufferMemoryConstraints& value);

std::ostream& operator<<(std::ostream& os, fuchsia::sysmem::PixelFormatType value);

std::ostream& operator<<(std::ostream& os, fuchsia::sysmem::FormatModifier value);

std::ostream& operator<<(std::ostream& os, const fuchsia::sysmem::PixelFormat& value);

std::ostream& operator<<(std::ostream& os, fuchsia::sysmem::ColorSpaceType value);

std::ostream& operator<<(std::ostream& os, const fuchsia::sysmem::ColorSpace& value);

std::ostream& operator<<(std::ostream& os, const fuchsia::sysmem::ImageFormatConstraints& value);

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::sysmem::BufferCollectionConstraints& value);

std::ostream& operator<<(std::ostream& os, const fuchsia::sysmem::ImageFormat_2& value);

}  // namespace test
}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FORMATTING_H_
