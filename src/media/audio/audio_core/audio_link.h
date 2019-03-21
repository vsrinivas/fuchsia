// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_LINK_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_LINK_H_

#include <fbl/intrusive_wavl_tree.h>
#include <fbl/ref_counted.h>

#include "lib/media/timeline/timeline_function.h"
#include "lib/media/timeline/timeline_rate.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/mixer/gain.h"
#include "src/media/audio/audio_core/mixer/mixer.h"

namespace media::audio {

class AudioDevice;
class AudioObject;

struct AudioLinkSourceTag {};
struct AudioLinkDestTag {};

// AudioLink is the base class of two different versions of AudioLinks which
// join sources of audio (AudioRenderers, inputs, outputs-being-looped-back) to
// destinations (outputs and AudioCapturers).
//
// TODO(mpuryear): Finish docs.
//
class AudioLink
    : public fbl::RefCounted<AudioLink>,
      public fbl::ContainableBaseClasses<
          fbl::WAVLTreeContainable<fbl::RefPtr<AudioLink>, AudioLinkSourceTag>,
          fbl::WAVLTreeContainable<fbl::RefPtr<AudioLink>, AudioLinkDestTag>> {
 protected:
  struct KeyTraits;

 public:
  using Source = AudioLinkSourceTag;
  using Dest = AudioLinkDestTag;

  template <typename TagType>
  using Set =
      fbl::TaggedWAVLTree<const AudioLink*, fbl::RefPtr<AudioLink>, TagType>;

  enum class SourceType {
    Packet,
    RingBuffer,
  };

  virtual ~AudioLink();

  const fbl::RefPtr<AudioObject>& GetSource() const { return source_; }
  const fbl::RefPtr<AudioObject>& GetDest() const { return dest_; }

  SourceType source_type() const { return source_type_; }

  // Sources invalidate links when they change format or go away.
  void Invalidate() { valid_.store(false); }
  bool valid() const { return valid_.load(); }

  // Bookkeeping access.
  const std::unique_ptr<Bookkeeping>& bookkeeping() const {
    return bookkeeping_;
  }
  void set_bookkeeping(std::unique_ptr<Bookkeeping> bookkeeping) {
    FXL_DCHECK(bookkeeping_ == nullptr);
    bookkeeping_ = std::move(bookkeeping);
  }

 protected:
  AudioLink(SourceType source_type, fbl::RefPtr<AudioObject> source,
            fbl::RefPtr<AudioObject> dest);

  friend struct fbl::DefaultKeyedObjectTraits<const AudioLink*, AudioLink>;
  const AudioLink* GetKey() const { return this; }

 private:
  const SourceType source_type_;
  fbl::RefPtr<AudioObject> source_;
  fbl::RefPtr<AudioObject> dest_;
  std::unique_ptr<Bookkeeping> bookkeeping_;
  std::atomic_bool valid_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_LINK_H_
