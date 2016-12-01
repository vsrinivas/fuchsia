// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <set>

namespace media {
namespace audio {

class AudioOutput;
class AudioOutputManager;
class AudioServerImpl;
class AudioRendererImpl;
class AudioRendererToOutputLink;

using AudioOutputPtr = std::shared_ptr<AudioOutput>;
using AudioOutputSet =
    std::set<AudioOutputPtr, std::owner_less<AudioOutputPtr>>;
using AudioOutputWeakPtr = std::weak_ptr<AudioOutput>;
using AudioOutputWeakSet =
    std::set<AudioOutputWeakPtr, std::owner_less<AudioOutputWeakPtr>>;

using AudioRendererImplPtr = std::shared_ptr<AudioRendererImpl>;
using AudioRendererImplSet =
    std::set<AudioRendererImplPtr, std::owner_less<AudioRendererImplPtr>>;
using AudioRendererImplWeakPtr = std::weak_ptr<AudioRendererImpl>;
using AudioRendererImplWeakSet =
    std::set<AudioRendererImplWeakPtr,
             std::owner_less<AudioRendererImplWeakPtr>>;

using AudioRendererToOutputLinkPtr = std::shared_ptr<AudioRendererToOutputLink>;
using AudioRendererToOutputLinkSet =
    std::set<AudioRendererToOutputLinkPtr,
             std::owner_less<AudioRendererToOutputLinkPtr>>;

}  // namespace audio
}  // namespace media
