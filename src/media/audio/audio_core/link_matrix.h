// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_LINK_MATRIX_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_LINK_MATRIX_H_

#include <fuchsia/media/cpp/fidl.h>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "src/media/audio/audio_core/audio_object.h"
#include "src/media/audio/audio_core/loudness_transform.h"

namespace media::audio {

// The link matrix contains a matrix of connections between audio objects.
// It handles establishing, storing, removing, and accessing links.
class LinkMatrix {
 public:
  struct LinkHandle {
    std::shared_ptr<AudioObject> object;
    std::shared_ptr<const LoudnessTransform> loudness_transform;
    std::shared_ptr<Stream> stream;
    std::shared_ptr<Mixer> mixer;
  };

  zx_status_t LinkObjects(std::shared_ptr<AudioObject> source, std::shared_ptr<AudioObject> dest,
                          std::shared_ptr<const LoudnessTransform> loudness_transform);

  void Unlink(const AudioObject& object);

  void ForEachDestLink(const AudioObject& object, fit::function<void(LinkHandle)> f);
  void ForEachSourceLink(const AudioObject& object, fit::function<void(LinkHandle)> f);

  size_t DestLinkCount(const AudioObject& object);
  size_t SourceLinkCount(const AudioObject& object);

  // Functions to retrieve the set of links for an object. Use the same vector each time to
  // skip unnecessary allocations.
  void DestLinks(const AudioObject& object, std::vector<LinkHandle>* store);
  void SourceLinks(const AudioObject& object, std::vector<LinkHandle>* store);

  // Returns true iff the source and dest are linked.
  bool AreLinked(const AudioObject& source, AudioObject& dest);

 private:
  struct Link {
    explicit Link(const AudioObject* _key) : key(_key) {}

    Link(std::shared_ptr<AudioObject> _object,
         std::shared_ptr<const LoudnessTransform> _loudness_transform,
         std::shared_ptr<Stream> _stream, std::shared_ptr<Mixer> _mixer)
        : key(_object.get()),
          object(std::move(_object)),
          loudness_transform(std::move(_loudness_transform)),
          stream(std::move(_stream)),
          mixer(std::move(_mixer)) {}

    bool operator==(const Link& rhs) const noexcept { return key == rhs.key; }

    const AudioObject* key;
    std::weak_ptr<AudioObject> object;
    std::shared_ptr<const LoudnessTransform> loudness_transform;
    std::shared_ptr<Stream> stream;
    std::shared_ptr<Mixer> mixer;
  };

  struct LinkHash {
    std::size_t operator()(Link const& link) const noexcept {
      return std::hash<const AudioObject*>{}(link.key);
    }
  };

  using LinkSet = std::unordered_set<Link, LinkHash>;

  // There may be a gap between an object dropping, and their removal from the matrix due
  // to there being no common enforced mechanism for the removal.
  //
  // We can remove this filter if there is a mechanism to enforce that dropped object immediately
  // remove themselves.
  void OnlyStrongLinks(LinkSet& link_set, std::vector<LinkHandle>* store);

  // Returns the link source set for the object. Inserts the key if it
  // is not already present in the matrix.
  LinkSet& SourceLinkSet(const AudioObject* object);
  LinkSet& DestLinkSet(const AudioObject* object);

  std::mutex lock_;

  std::unordered_map<const AudioObject*, LinkSet> sources_ FXL_GUARDED_BY(lock_);
  std::unordered_map<const AudioObject*, LinkSet> dests_ FXL_GUARDED_BY(lock_);
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_LINK_MATRIX_H_
