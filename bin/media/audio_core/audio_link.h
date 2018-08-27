// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_CORE_AUDIO_LINK_H_
#define GARNET_BIN_MEDIA_AUDIO_CORE_AUDIO_LINK_H_

#include <fbl/ref_ptr.h>

#include "garnet/bin/media/audio_core/gain.h"
#include "garnet/bin/media/audio_core/mixer/mixer.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/media/timeline/timeline_function.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media {
namespace audio {

class AudioDevice;
class AudioObject;

// AudioLink is the base class of two different versions of AudioLinks which
// join sources of audio (audio outs, inputs, and loop-backed outputs) to
// destinations (outputs and audio ins).
//
// TODO(johngro): Finish docs.
// TODO(johngro): Manage link tracking using instrusive pointers.  Intrusive
// storage goes at this level.
//
class AudioLink {
 public:
  enum class SourceType {
    Packet,
    RingBuffer,
  };

  // TODO(mpuryear): per MTWN-129, integrate this into the Mixer class itself.
  // TODO(mpuryear): Rationalize naming and usage of the bookkeeping structs.
  struct Bookkeeping {
    Bookkeeping() = default;
    ~Bookkeeping() = default;

    MixerPtr mixer;
    Gain::AScale amplitude_scale;

    uint32_t step_size;
    uint32_t modulo;
    uint32_t denominator() const {
      return dest_frames_to_frac_source_frames.rate().reference_delta();
    }

    // The output values of these functions are in fractional frames.
    TimelineFunction dest_frames_to_frac_source_frames;
    uint32_t dest_trans_gen_id = kInvalidGenerationId;

    // TimelineFunction clock_mono_to_src_frames;
    TimelineFunction clock_mono_to_frac_source_frames;
    uint32_t source_trans_gen_id = kInvalidGenerationId;
  };

  virtual ~AudioLink();

  const fbl::RefPtr<AudioObject>& GetSource() { return source_; }
  const fbl::RefPtr<AudioObject>& GetDest() { return dest_; }

  SourceType source_type() const { return source_type_; }

  // Accessor for the link's gain state tracking class.  Used by both the main
  // message loop thread and the mixer threads.
  Gain& gain() { return gain_; }

  // Current validity.  Sources invalidate links when they either go away, or
  // change formats.
  void Invalidate() { valid_.store(false); }
  bool valid() const { return valid_.load(); }

  // Bookkeeping access.
  const std::unique_ptr<Bookkeeping>& bookkeeping() {
    return bookkeeping_;
  }
  void set_bookkeeping(std::unique_ptr<Bookkeeping> bookkeeping) {
    FXL_DCHECK(bookkeeping_ == nullptr);
    bookkeeping_ = std::move(bookkeeping);
  }

 protected:
  AudioLink(SourceType source_type, fbl::RefPtr<AudioObject> source,
            fbl::RefPtr<AudioObject> dest);

 private:
  const SourceType source_type_;
  fbl::RefPtr<AudioObject> source_;
  fbl::RefPtr<AudioObject> dest_;
  std::unique_ptr<Bookkeeping> bookkeeping_;
  Gain gain_;
  std::atomic_bool valid_;
};

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_CORE_AUDIO_LINK_H_
