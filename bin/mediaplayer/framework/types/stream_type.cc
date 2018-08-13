// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/framework/types/stream_type.h"

#include "garnet/bin/mediaplayer/util/safe_clone.h"
#include "lib/fxl/logging.h"

namespace media_player {

// These must match the definitions in media_types.fidl. This is verfied by
// the KnownEncodingsMatch function in fidl/fidl_type_conversion.cc. Changes
// to this list should be reflected there.
const char StreamType::kMediaEncodingUnsupported[] = "unsupported";

const char StreamType::kAudioEncodingAac[] = "aac";
const char StreamType::kAudioEncodingAmrNb[] = "amrnb";
const char StreamType::kAudioEncodingAmrWb[] = "amrwb";
const char StreamType::kAudioEncodingFlac[] = "flac";
const char StreamType::kAudioEncodingGsmMs[] = "gsmms";
const char StreamType::kAudioEncodingLpcm[] = "lpcm";
const char StreamType::kAudioEncodingMp3[] = "mp3";
const char StreamType::kAudioEncodingPcmALaw[] = "pcmalaw";
const char StreamType::kAudioEncodingPcmMuLaw[] = "pcmmulaw";
const char StreamType::kAudioEncodingVorbis[] = "vorbis";

const char StreamType::kVideoEncodingH263[] = "h263";
const char StreamType::kVideoEncodingH264[] = "h264";
const char StreamType::kVideoEncodingMpeg4[] = "mpeg4";
const char StreamType::kVideoEncodingTheora[] = "theora";
const char StreamType::kVideoEncodingUncompressed[] = "uncompressed_video";
const char StreamType::kVideoEncodingVp3[] = "vp3";
const char StreamType::kVideoEncodingVp8[] = "vp8";
const char StreamType::kVideoEncodingVp9[] = "vp9";

StreamType::StreamType(Medium medium, const std::string& encoding,
                       std::unique_ptr<Bytes> encoding_parameters)
    : medium_(medium),
      encoding_(encoding),
      encoding_parameters_(std::move(encoding_parameters)) {}

StreamType::~StreamType() {}

const AudioStreamType* StreamType::audio() const {
  FXL_LOG(ERROR) << "audio method called on non-audio stream type";
  return nullptr;
}

const VideoStreamType* StreamType::video() const {
  FXL_LOG(ERROR) << "video method called on non-video stream type";
  return nullptr;
}

const TextStreamType* StreamType::text() const {
  FXL_LOG(ERROR) << "text method called on non-text stream type";
  return nullptr;
}

const SubpictureStreamType* StreamType::subpicture() const {
  FXL_LOG(ERROR) << "subpicture method called on non-subpicture stream type";
  return nullptr;
}

std::unique_ptr<StreamType> StreamType::Clone() const {
  return Create(medium(), encoding(), SafeClone(encoding_parameters()));
}

StreamTypeSet::StreamTypeSet(StreamType::Medium medium,
                             const std::vector<std::string>& encodings)
    : medium_(medium), encodings_(encodings) {}

StreamTypeSet::~StreamTypeSet() {}

const AudioStreamTypeSet* StreamTypeSet::audio() const {
  FXL_LOG(ERROR) << "audio method called on non-audio stream type set";
  return nullptr;
}

const VideoStreamTypeSet* StreamTypeSet::video() const {
  FXL_LOG(ERROR) << "video method called on non-video stream type set";
  return nullptr;
}

const TextStreamTypeSet* StreamTypeSet::text() const {
  FXL_LOG(ERROR) << "text method called on non-text stream type set";
  return nullptr;
}

const SubpictureStreamTypeSet* StreamTypeSet::subpicture() const {
  FXL_LOG(ERROR)
      << "subpicture method called on non-subpicture stream type set";
  return nullptr;
}

std::unique_ptr<StreamTypeSet> StreamTypeSet::Clone() const {
  return Create(medium(), encodings());
}

bool StreamTypeSet::IncludesEncoding(const std::string& encoding) const {
  for (const std::string set_encoding : encodings_) {
    if (set_encoding == encoding) {
      return true;
    }
  }

  return false;
}

bool StreamTypeSet::Includes(const StreamType& type) const {
  return medium_ == type.medium() && IncludesEncoding(type.encoding());
}

}  // namespace media_player
