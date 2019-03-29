// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/graph/types/stream_type.h"

#include "src/lib/fxl/logging.h"
#include "src/media/playback/mediaplayer/util/safe_clone.h"

namespace media_player {

// These must match the definitions in media_types.fidl. This is verfied by
// the KnownEncodingsMatch function in fidl/fidl_type_conversion.cc. Changes
// to this list should be reflected there.
const char StreamType::kMediaEncodingUnsupported[] =
    "fuchsia.media.unsupported";

const char StreamType::kAudioEncodingAac[] = "fuchsia.media.aac";
const char StreamType::kAudioEncodingAmrNb[] = "fuchsia.media.amrnb";
const char StreamType::kAudioEncodingAmrWb[] = "fuchsia.media.amrwb";
const char StreamType::kAudioEncodingAptX[] = "fuchsia.media.aptx";
const char StreamType::kAudioEncodingFlac[] = "fuchsia.media.flac";
const char StreamType::kAudioEncodingGsmMs[] = "fuchsia.media.gsmms";
const char StreamType::kAudioEncodingLpcm[] = "fuchsia.media.lpcm";
const char StreamType::kAudioEncodingMp3[] = "fuchsia.media.mp3";
const char StreamType::kAudioEncodingPcmALaw[] = "fuchsia.media.pcmalaw";
const char StreamType::kAudioEncodingPcmMuLaw[] = "fuchsia.media.pcmmulaw";
const char StreamType::kAudioEncodingSbc[] = "fuchsia.media.sbc";
const char StreamType::kAudioEncodingVorbis[] = "fuchsia.media.vorbis";

const char StreamType::kVideoEncodingH263[] = "fuchsia.media.h263";
const char StreamType::kVideoEncodingH264[] = "fuchsia.media.h264";
const char StreamType::kVideoEncodingMpeg4[] = "fuchsia.media.mpeg4";
const char StreamType::kVideoEncodingTheora[] = "fuchsia.media.theora";
const char StreamType::kVideoEncodingUncompressed[] =
    "fuchsia.media.uncompressed_video";
const char StreamType::kVideoEncodingVp3[] = "fuchsia.media.vp3";
const char StreamType::kVideoEncodingVp8[] = "fuchsia.media.vp8";
const char StreamType::kVideoEncodingVp9[] = "fuchsia.media.vp9";

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
