// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_FORMAT2_STREAM_CONVERTER_H_
#define SRC_MEDIA_AUDIO_LIB_FORMAT2_STREAM_CONVERTER_H_

#include <memory>

#include "src/media/audio/lib/format2/format.h"

namespace media_audio {

// Converts a stream of audio from a source sample format to a destination sample format.
class StreamConverter {
 public:
  ~StreamConverter();
  StreamConverter(StreamConverter&&);
  StreamConverter& operator=(StreamConverter&&);

  // The source and destination formats must have matching frame rates and channel counts.
  static std::shared_ptr<StreamConverter> Create(const Format& source_format,
                                                 const Format& dest_format);

  // Like Create, but assume the source format is `float`.
  // TODO(fxbug.dev/114920): remove when old audio_core code is gone
  static std::shared_ptr<StreamConverter> CreateFromFloatSource(const Format& dest_format);

  const Format& source_format() const { return source_format_; }
  const Format& dest_format() const { return dest_format_; }

  // Converts `frame_count` frames in `source_data` from the source format into the destination
  // format, then writes the converted data into `dest_data`.
  void Copy(const void* source_data, void* dest_data, int64_t frame_count) const;

  // Liky Copy, but also clips the output when the destination format uses floating point samples.
  void CopyAndClip(const void* source_data, void* dest_data, int64_t frame_count) const;

  // Writes `frame_count` silent frames to `dest_data`.
  void WriteSilence(void* dest_data, int64_t frame_count) const;

  // Implementation detail.
  class CopyImpl;

 private:
  static std::shared_ptr<StreamConverter> Create(const Format& source_format,
                                                 const Format& dest_format,
                                                 std::unique_ptr<CopyImpl> copy_impl);

  StreamConverter(const Format& source_format, const Format& dest_format,
                  std::unique_ptr<CopyImpl> copy_impl);

  StreamConverter(const StreamConverter&) = delete;
  StreamConverter& operator=(const StreamConverter&) = delete;

  Format source_format_;  // non-const to allow a default move ctor
  Format dest_format_;    // non-const to allow a default move ctor
  std::unique_ptr<CopyImpl> copy_impl_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_FORMAT2_STREAM_CONVERTER_H_
