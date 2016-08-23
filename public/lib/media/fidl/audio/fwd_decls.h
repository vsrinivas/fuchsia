// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_AUDIO_FWD_DECLS_H__
#define SERVICES_MEDIA_AUDIO_FWD_DECLS_H__

#include <memory>
#include <set>

namespace mojo {
namespace media {
namespace audio {

class AudioOutput;
class AudioOutputManager;
class AudioServerImpl;
class AudioTrackImpl;
class AudioTrackToOutputLink;

using AudioOutputPtr = std::shared_ptr<AudioOutput>;
using AudioOutputSet =
    std::set<AudioOutputPtr, std::owner_less<AudioOutputPtr>>;
using AudioOutputWeakPtr = std::weak_ptr<AudioOutput>;
using AudioOutputWeakSet =
    std::set<AudioOutputWeakPtr, std::owner_less<AudioOutputWeakPtr>>;

using AudioTrackImplPtr = std::shared_ptr<AudioTrackImpl>;
using AudioTrackImplSet =
    std::set<AudioTrackImplPtr, std::owner_less<AudioTrackImplPtr>>;
using AudioTrackImplWeakPtr = std::weak_ptr<AudioTrackImpl>;
using AudioTrackImplWeakSet =
    std::set<AudioTrackImplWeakPtr, std::owner_less<AudioTrackImplWeakPtr>>;

using AudioTrackToOutputLinkPtr = std::shared_ptr<AudioTrackToOutputLink>;
using AudioTrackToOutputLinkSet =
    std::set<AudioTrackToOutputLinkPtr,
             std::owner_less<AudioTrackToOutputLinkPtr>>;

}  // namespace audio
}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_AUDIO_FWD_DECLS_H__
