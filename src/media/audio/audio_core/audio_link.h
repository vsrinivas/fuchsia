// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_LINK_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_LINK_H_

#include <memory>

#include <fbl/intrusive_wavl_tree.h>
#include <fbl/ref_counted.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/media/audio/audio_core/mixer/mixer.h"
#include "src/media/audio/audio_core/stream.h"
#include "src/media/audio/audio_core/volume_curve.h"

namespace media::audio {
class AudioDevice;
class AudioObject;

struct AudioLinkSourceTag {};
struct AudioLinkDestTag {};

// AudioLink is the base class of two different versions of AudioLinks which join sources of audio
// (AudioRenderers, inputs, outputs-being-looped-back) to destinations (outputs and
// AudioCapturers).
//
// TODO(mpuryear): Finish docs.
//
class AudioLink : public fbl::RefCounted<AudioLink>,
                  public fbl::ContainableBaseClasses<
                      fbl::WAVLTreeContainable<fbl::RefPtr<AudioLink>, AudioLinkSourceTag>,
                      fbl::WAVLTreeContainable<fbl::RefPtr<AudioLink>, AudioLinkDestTag>> {
 protected:
  struct KeyTraits;

 public:
  static fbl::RefPtr<AudioLink> Create(fbl::RefPtr<AudioObject> source,
                                       fbl::RefPtr<AudioObject> dest);

  using Source = AudioLinkSourceTag;
  using Dest = AudioLinkDestTag;

  template <typename TagType>
  using Set = fbl::TaggedWAVLTree<const AudioLink*, fbl::RefPtr<AudioLink>, TagType>;

  AudioLink(fbl::RefPtr<AudioObject> source, fbl::RefPtr<AudioObject> dest);

  const fbl::RefPtr<AudioObject>& GetSource() const { return source_; }
  const fbl::RefPtr<AudioObject>& GetDest() const { return dest_; }

  // The VolumeCurve of the link, representing either the source or destination's mapping from
  // volume to gain. Both ends of a link cannot have mappings as this would be irreconcilable.
  const VolumeCurve& volume_curve() const;
  Gain& gain() { return mixer()->bookkeeping().gain; }
  const fbl::RefPtr<Stream>& stream() const { return stream_; }
  void set_stream(fbl::RefPtr<Stream> stream) { stream_ = std::move(stream); }

  // Sources invalidate links when they change format or go away.
  void Invalidate() { valid_.store(false); }
  bool valid() const { return valid_.load(); }

  // Mixer access.
  Mixer* mixer() const { return mixer_.get(); }
  void set_mixer(std::shared_ptr<Mixer> mixer) { mixer_ = std::move(mixer); }

 protected:
  friend struct fbl::DefaultKeyedObjectTraits<const AudioLink*, AudioLink>;
  const AudioLink* GetKey() const { return this; }

 private:
  fbl::RefPtr<AudioObject> source_;
  fbl::RefPtr<AudioObject> dest_;
  std::shared_ptr<Mixer> mixer_;
  std::atomic_bool valid_;
  const std::optional<VolumeCurve> volume_curve_;
  fbl::RefPtr<Stream> stream_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_LINK_H_
