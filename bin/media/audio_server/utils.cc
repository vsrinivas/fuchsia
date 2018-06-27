// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/utils.h"

#include <audio-proto-utils/format-utils.h>

#include "garnet/bin/media/audio_server/driver_utils.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {

zx_status_t SelectBestFormat(
    const std::vector<audio_stream_format_range_t>& fmts,
    uint32_t* frames_per_second_inout, uint32_t* channels_inout,
    fuchsia::media::AudioSampleFormat* sample_format_inout) {
  if ((frames_per_second_inout == nullptr) || (channels_inout == nullptr) ||
      (sample_format_inout == nullptr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t pref_frame_rate = *frames_per_second_inout;
  uint32_t pref_channels = *channels_inout;
  audio_sample_format_t pref_sample_format;

  if (!driver_utils::AudioSampleFormatToDriverSampleFormat(
          *sample_format_inout, &pref_sample_format)) {
    FXL_LOG(WARNING) << "Failed to convert FIDL sample format ("
                     << static_cast<uint32_t>(*sample_format_inout)
                     << ") to driver sample format.";
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t best_frame_rate;
  uint32_t best_channels;
  audio_sample_format_t best_sample_format;
  uint32_t best_score = 0;
  uint32_t best_frame_rate_delta = std::numeric_limits<uint32_t>::max();

  constexpr uint32_t U8_FMT =
      AUDIO_SAMPLE_FORMAT_8BIT | AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED;
  constexpr uint32_t S16_FMT = AUDIO_SAMPLE_FORMAT_16BIT;
  constexpr uint32_t F32_FMT = AUDIO_SAMPLE_FORMAT_32BIT_FLOAT;

  // For now, users should only ask for unsigned-8, signed-16 or float-32. If
  // they ask for anything else, change their preference to signed-16.
  //
  // TODO(johngro) : clean this up as part of fixing MTWN-54
  if ((pref_sample_format & AUDIO_SAMPLE_FORMAT_FLAG_INVERT_ENDIAN) ||
      (((pref_sample_format & U8_FMT) != U8_FMT) &&
       ((pref_sample_format & S16_FMT) != S16_FMT) &&
       ((pref_sample_format & F32_FMT) != F32_FMT))) {
    pref_sample_format = AUDIO_SAMPLE_FORMAT_16BIT;
  }

  for (const auto& range : fmts) {
    // Start by scoring our sample format.  Right now, we only support 8-bit
    // unsigned, 16-bit signed and 32-bit float in the mixer.  If this
    // sample format range does not support any of these, just skip it for now.
    // Otherwise, 4 points if you match the requested format, 3 for signed-16,
    // 2 for float-32, or 1 for unsigned-8.
    //
    // TODO(mpuryear): once we can validate float-32 with hardware that natively
    // handles it, change this algorithm to prefer float-32 over signed-16.
    audio_sample_format_t this_sample_format;
    int sample_format_score;

    bool supports_u8 = (range.sample_formats & U8_FMT) == U8_FMT;
    bool supports_s16 = (range.sample_formats & S16_FMT) == S16_FMT;
    bool supports_f32 = (range.sample_formats & F32_FMT) == F32_FMT;
    if ((range.sample_formats & AUDIO_SAMPLE_FORMAT_FLAG_INVERT_ENDIAN) ||
        (!supports_u8 && !supports_s16 && !supports_f32)) {
      continue;  // Skip -- this isn't a sample container we understand.
    }

    if ((pref_sample_format & range.sample_formats) == pref_sample_format) {
      // Direct match.
      this_sample_format = pref_sample_format;
      sample_format_score = 4;
    } else if (supports_s16) {
      this_sample_format = AUDIO_SAMPLE_FORMAT_16BIT;
      sample_format_score = 3;
    } else if (supports_f32) {
      this_sample_format = AUDIO_SAMPLE_FORMAT_32BIT_FLOAT;
      sample_format_score = 2;
    } else {
      FXL_DCHECK(supports_u8);
      this_sample_format = static_cast<audio_sample_format_t>(U8_FMT);
      sample_format_score = 1;
    }

    // Next consider the supported channel counts.  3 points for matching the
    // requested channel count.  Otherwise, default to stereo (if supported) and
    // score 2 points.  Failing that, just pick the top end of the supported
    // channel range and score 1 point.
    uint32_t this_channels;
    int channel_count_score;
    if ((pref_channels >= range.min_channels) &&
        (pref_channels <= range.max_channels)) {
      this_channels = pref_channels;
      channel_count_score = 3;
    } else if ((2 >= range.min_channels) && (2 <= range.max_channels)) {
      this_channels = 2;
      channel_count_score = 2;
    } else {
      this_channels = range.max_channels;
      channel_count_score = 1;
    }

    // Next score based on supported frame rates.  Score 3 points for a match, 2
    // points if we have to scale up to the nearest supported rate, or 1 point
    // if we have to scale down.
    //
    if (range.min_frames_per_second > range.max_frames_per_second) {
      // Skip this frame rate range entirely if it is empty.
      FXL_DLOG(WARNING) << "Skipping empty frame rate range ["
                        << range.min_frames_per_second << ", "
                        << range.max_frames_per_second
                        << "] while searching for best format in driver list.";
      continue;
    }

    uint32_t this_frame_rate = 0;
    uint32_t frame_rate_delta = std::numeric_limits<uint32_t>::max();
    int frame_rate_score = 0;
    if (range.flags & ASF_RANGE_FLAG_FPS_CONTINUOUS) {
      // This is a continuous sample rate range.  If we are within the range,
      // thats a match.  Otherwise move up/down as needed to match the min/max
      // of the range as appropriate.
      if ((pref_frame_rate >= range.min_frames_per_second) &&
          (pref_frame_rate <= range.max_frames_per_second)) {
        this_frame_rate = pref_frame_rate;
        frame_rate_score = 3;
        frame_rate_delta = 0;
      } else if (pref_frame_rate < range.min_frames_per_second) {
        this_frame_rate = range.min_frames_per_second;
        frame_rate_score = 2;
        frame_rate_delta = range.min_frames_per_second - pref_frame_rate;
      } else {
        FXL_DCHECK(pref_frame_rate > range.max_frames_per_second);
        this_frame_rate = range.max_frames_per_second;
        frame_rate_score = 1;
        frame_rate_delta = pref_frame_rate - range.max_frames_per_second;
      }
    } else {
      // This is a discrete sample rate range.  Use the frame rate enumerator
      // utility class to evaluate each of the possible frame rates.
      for (const uint32_t rate : ::audio::utils::FrameRateEnumerator(range)) {
        if (pref_frame_rate == rate) {
          // We matched our preference.  No need to keep searching.
          this_frame_rate = rate;
          frame_rate_score = 3;
          frame_rate_delta = 0;
          break;
        }

        if (pref_frame_rate < rate) {
          // Scaling up; 2 points.  If this better than what we were doing
          // before, just choose it.  If we were already going to scale up,
          // pick the frame rate which is closer to our preference.
          // Otherwise, do nothing.
          if ((frame_rate_score < 2) ||
              ((frame_rate_score == 2) && (rate < this_frame_rate))) {
            this_frame_rate = rate;
            frame_rate_score = 2;
            frame_rate_delta = rate - pref_frame_rate;
          }
        } else {
          FXL_DCHECK(pref_frame_rate > rate);

          // Scaling down; 1 point.  If this better than what we were doing
          // before, just choose it.  If we were already going to scale down,
          // pick the frame rate which is closer to our preference.
          // Otherwise, do nothing.
          if ((frame_rate_score < 1) ||
              ((frame_rate_score == 1) && (rate > this_frame_rate))) {
            this_frame_rate = rate;
            frame_rate_score = 1;
            frame_rate_delta = pref_frame_rate - rate;
          }
        }
      }
    }

    // If our frame rate score is still zero, it means that we were given a
    // discrete frame range by a driver which was completely empty (even
    // though min was <= max as it should be)  Debug log a warning, then skip
    // the range entirely.
    if (frame_rate_score == 0) {
      FXL_DLOG(WARNING) << "Skipping empty discrete frame rate range ["
                        << range.min_frames_per_second << ", "
                        << range.max_frames_per_second << "] (flags "
                        << range.flags << ") while searching for best format";
      continue;
    }

    // OK, we have computed the best option supported by this frame rate range.
    // Weight the score, and it if it better then any of our previous best
    // score, replace our previous best with this.
    uint32_t score;
    score = (sample_format_score * 100)   // format is the most important.
            + (channel_count_score * 10)  // channel count comes second.
            + frame_rate_score;           // frame rate is the least important.

    FXL_DCHECK(score > 0);
    FXL_DCHECK(::audio::utils::FormatIsCompatible(
        this_frame_rate, this_channels, this_sample_format, range));

    // If this score is better than the current best score, or this score ties
    // the current best score but the frame rate distance is less, then this is
    // the new best format.
    if ((score > best_score) ||
        ((score == best_score) && (frame_rate_delta < best_frame_rate_delta))) {
      best_frame_rate = this_frame_rate;
      best_frame_rate_delta = frame_rate_delta;
      best_channels = this_channels;
      best_sample_format = this_sample_format;
      best_score = score;
    }
  }

  // If our score is still zero, then there must have be absolutely no supported
  // formats in the set provided by the driver.
  if (!best_score) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  __UNUSED bool convert_res =
      driver_utils::DriverSampleFormatToAudioSampleFormat(best_sample_format,
                                                          sample_format_inout);
  FXL_DCHECK(convert_res);

  *channels_inout = best_channels;
  *frames_per_second_inout = best_frame_rate;

  return ZX_OK;
}

}  // namespace audio
}  // namespace media
