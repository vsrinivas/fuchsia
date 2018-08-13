// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FRAMEWORK_TYPES_STREAM_TYPE_H_
#define GARNET_BIN_MEDIAPLAYER_FRAMEWORK_TYPES_STREAM_TYPE_H_

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "garnet/bin/mediaplayer/framework/types/bytes.h"
#include "lib/fxl/logging.h"

namespace media_player {

class StreamType;
class AudioStreamType;
class VideoStreamType;
class TextStreamType;
class SubpictureStreamType;

// Describes the type of a stream.
class StreamType {
 public:
  enum class Medium { kAudio, kVideo, kText, kSubpicture };

  static const char kMediaEncodingUnsupported[];

  static const char kAudioEncodingAac[];
  static const char kAudioEncodingAmrNb[];
  static const char kAudioEncodingAmrWb[];
  static const char kAudioEncodingFlac[];
  static const char kAudioEncodingGsmMs[];
  static const char kAudioEncodingLpcm[];
  static const char kAudioEncodingMp3[];
  static const char kAudioEncodingPcmALaw[];
  static const char kAudioEncodingPcmMuLaw[];
  static const char kAudioEncodingVorbis[];

  static const char kVideoEncodingH263[];
  static const char kVideoEncodingH264[];
  static const char kVideoEncodingMpeg4[];
  static const char kVideoEncodingTheora[];
  static const char kVideoEncodingUncompressed[];
  static const char kVideoEncodingVp3[];
  static const char kVideoEncodingVp8[];
  static const char kVideoEncodingVp9[];

  static std::unique_ptr<StreamType> Create(
      Medium medium, const std::string& encoding,
      std::unique_ptr<Bytes> encoding_parameters) {
    return std::unique_ptr<StreamType>(
        new StreamType(medium, encoding, std::move(encoding_parameters)));
  }

  StreamType(Medium medium, const std::string& encoding,
             std::unique_ptr<Bytes> encoding_parameters);

  virtual ~StreamType();

  Medium medium() const { return medium_; }

  const std::string& encoding() const { return encoding_; }

  const std::unique_ptr<Bytes>& encoding_parameters() const {
    return encoding_parameters_;
  }

  virtual const AudioStreamType* audio() const;
  virtual const VideoStreamType* video() const;
  virtual const TextStreamType* text() const;
  virtual const SubpictureStreamType* subpicture() const;

  virtual std::unique_ptr<StreamType> Clone() const;

 private:
  Medium medium_;
  std::string encoding_;
  std::unique_ptr<Bytes> encoding_parameters_;
};

template <typename T>
struct Range {
  Range(T min_param, T max_param) : min(min_param), max(max_param) {
    FXL_DCHECK(min_param <= max_param);
  }

  T min;
  T max;

  constexpr bool contains(const T& t) const { return t >= min && t <= max; }
};

class StreamTypeSet;
class AudioStreamTypeSet;
class VideoStreamTypeSet;
class TextStreamTypeSet;
class SubpictureStreamTypeSet;

// Describes a set of possible stream types.
class StreamTypeSet {
 public:
  static std::unique_ptr<StreamTypeSet> Create(
      StreamType::Medium medium, const std::vector<std::string>& encodings) {
    return std::unique_ptr<StreamTypeSet>(new StreamTypeSet(medium, encodings));
  }

  StreamTypeSet(StreamType::Medium medium,
                const std::vector<std::string>& encodings);

  virtual ~StreamTypeSet();

  StreamType::Medium medium() const { return medium_; }

  const std::vector<std::string>& encodings() const { return encodings_; }

  virtual const AudioStreamTypeSet* audio() const;
  virtual const VideoStreamTypeSet* video() const;
  virtual const TextStreamTypeSet* text() const;
  virtual const SubpictureStreamTypeSet* subpicture() const;

  virtual std::unique_ptr<StreamTypeSet> Clone() const;

  bool IncludesEncoding(const std::string& encoding) const;

  virtual bool Includes(const StreamType& type) const;

 private:
  StreamType::Medium medium_;
  std::vector<std::string> encodings_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FRAMEWORK_TYPES_STREAM_TYPE_H_
