// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/video_codecs.h"

namespace media {

std::string GetProfileName(VideoCodecProfile profile) {
  switch (profile) {
    case VIDEO_CODEC_PROFILE_UNKNOWN:
      return "unknown";
    case H264PROFILE_BASELINE:
      return "h264 baseline";
    case H264PROFILE_MAIN:
      return "h264 main";
    case H264PROFILE_EXTENDED:
      return "h264 extended";
    case H264PROFILE_HIGH:
      return "h264 high";
    case H264PROFILE_HIGH10PROFILE:
      return "h264 high 10";
    case H264PROFILE_HIGH422PROFILE:
      return "h264 high 4:2:2";
    case H264PROFILE_HIGH444PREDICTIVEPROFILE:
      return "h264 high 4:4:4 predictive";
    case H264PROFILE_SCALABLEBASELINE:
      return "h264 scalable baseline";
    case H264PROFILE_SCALABLEHIGH:
      return "h264 scalable high";
    case H264PROFILE_STEREOHIGH:
      return "h264 stereo high";
    case H264PROFILE_MULTIVIEWHIGH:
      return "h264 multiview high";
    case HEVCPROFILE_MAIN:
      return "hevc main";
    case HEVCPROFILE_MAIN10:
      return "hevc main 10";
    case HEVCPROFILE_MAIN_STILL_PICTURE:
      return "hevc main still-picture";
    case VP8PROFILE_ANY:
      return "vp8";
    case VP9PROFILE_PROFILE0:
      return "vp9 profile0";
    case VP9PROFILE_PROFILE1:
      return "vp9 profile1";
    case VP9PROFILE_PROFILE2:
      return "vp9 profile2";
    case VP9PROFILE_PROFILE3:
      return "vp9 profile3";
    case DOLBYVISION_PROFILE0:
      return "dolby vision profile 0";
    case DOLBYVISION_PROFILE4:
      return "dolby vision profile 4";
    case DOLBYVISION_PROFILE5:
      return "dolby vision profile 5";
    case DOLBYVISION_PROFILE7:
      return "dolby vision profile 7";
    case DOLBYVISION_PROFILE8:
      return "dolby vision profile 8";
    case DOLBYVISION_PROFILE9:
      return "dolby vision profile 9";
    case THEORAPROFILE_ANY:
      return "theora";
    case AV1PROFILE_PROFILE_MAIN:
      return "av1 profile main";
    case AV1PROFILE_PROFILE_HIGH:
      return "av1 profile high";
    case AV1PROFILE_PROFILE_PRO:
      return "av1 profile pro";
  }
  NOTREACHED();
  return "";
}

}  // namespace media
