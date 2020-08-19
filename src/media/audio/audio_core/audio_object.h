// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_OBJECT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_OBJECT_H_

#include <lib/fit/function.h>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/mixer/no_op.h"
#include "src/media/audio/audio_core/stream.h"
#include "src/media/audio/audio_core/stream_usage.h"
#include "src/media/audio/audio_core/threading_model.h"
#include "src/media/audio/audio_core/volume_curve.h"
#include "src/media/audio/lib/format/format.h"

namespace media::audio {

// The simple base class for 4 major types of audio objects in the mixer: Outputs, Inputs,
// AudioRenderers and AudioCapturers. It ensures that each is intrusively ref-counted, and remembers
// its type so that it may be safely downcast from generic object to something more specific.
class AudioObject {
 public:
  // Disallow copy, assign, and move.
  AudioObject& operator=(AudioObject) = delete;
  AudioObject(const AudioObject&) = delete;
  AudioObject(AudioObject&&) = delete;
  AudioObject& operator=(AudioObject&&) = delete;

  virtual ~AudioObject() = default;

  enum class Type {
    Output,
    Input,
    AudioRenderer,
    AudioCapturer,
  };

  // Initialize(Source|Dest)Link
  //
  // Called on the AudioCore's main message loop any time a source and a destination are being
  // linked via AudioObject::LinkObjects. By default, these hooks do nothing, but AudioObject
  // subclasses may use them to set the properties of a link (or reject the link) before the link
  // gets added to the source and destination link sets.
  //
  // For example, Sources like an AudioRenderer override InitializeDestLink in order to set the
  // source gain and to make a copy of their pending packet queue. Destinations like an output
  // override InitializeSourceLink in order to choose and initialize an appropriate resampling
  // filter.
  //
  // When initializing a source link, an implementor must provide a mixer and an ExecutionDomain
  // for that mixer to run in. The source object and their stream are provided.
  //
  // Returns ZX_OK if initialization succeeded, or an appropriate error code otherwise.
  virtual fit::result<std::pair<std::shared_ptr<Mixer>, ExecutionDomain*>, zx_status_t>
  InitializeSourceLink(const AudioObject& source, std::shared_ptr<ReadableStream> stream) {
    return fit::ok(std::make_pair(std::make_shared<audio::mixer::NoOp>(), nullptr));
  }
  virtual fit::result<std::shared_ptr<ReadableStream>, zx_status_t> InitializeDestLink(
      const AudioObject& dest) {
    return fit::ok(nullptr);
  }

  virtual void CleanupSourceLink(const AudioObject& source,
                                 std::shared_ptr<ReadableStream> stream) {}
  virtual void CleanupDestLink(const AudioObject& dest) {}

  // Called immediately after a new link is added to the object.
  virtual void OnLinkAdded() {}

  // Note: format() is subject to change and must only be accessed from the main message loop
  // thread. Outputs which are running on mixer threads should never access format() directly
  // from a mix thread. Instead, they should use the format which was assigned to the AudioLink
  // at the time the link was created.
  virtual std::optional<Format> format() const { return std::nullopt; }

  virtual std::optional<StreamUsage> usage() const { return std::nullopt; }

  bool format_valid() const { return format().has_value(); }

  Type type() const { return type_; }
  bool is_output() const { return type() == Type::Output; }
  bool is_input() const { return type() == Type::Input; }
  bool is_audio_renderer() const { return type() == Type::AudioRenderer; }
  bool is_audio_capturer() const { return type() == Type::AudioCapturer; }

 protected:
  explicit AudioObject(Type type) : type_(type) {}

 private:
  const Type type_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_OBJECT_H_
