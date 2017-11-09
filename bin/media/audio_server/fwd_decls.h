// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_ptr.h>
#include <memory>
#include <set>

namespace media {
namespace audio {

class AudioDeviceManager;
class AudioServerImpl;
class AudioRendererFormatInfo;
class AudioRendererImpl;
class AudioLink;

// TODO(johngro) : Remove these definitions when we move to intrusive containers
// for managing links.
using AudioLinkPtr = std::shared_ptr<AudioLink>;
using AudioLinkSet = std::set<AudioLinkPtr, std::owner_less<AudioLinkPtr>>;

}  // namespace audio
}  // namespace media
