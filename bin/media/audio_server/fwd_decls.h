// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <fbl/ref_ptr.h>
#include <set>

namespace media {
namespace audio {

class AudioOutputManager;
class AudioServerImpl;
class AudioRendererFormatInfo;
class AudioRendererImpl;
class AudioRendererToOutputLink;

// TODO(johngro) : Remove these aliases and move to a style where we always
// explicitly declare our managed pointer types.
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
