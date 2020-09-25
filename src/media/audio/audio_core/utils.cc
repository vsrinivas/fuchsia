// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/utils.h"

#include <fuchsia/scheduler/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/channel.h>

#include <cstdlib>

#include <audio-proto-utils/format-utils.h>

#include "src/media/audio/audio_core/threading_model.h"
#include "src/media/audio/lib/format/driver_format.h"

namespace media::audio {

bool IsSampleFormatInSupported(
    fuchsia::hardware::audio::SampleFormat sample_format, uint8_t bytes_per_sample,
    uint8_t valid_bits_per_sample,
    const fuchsia::hardware::audio::PcmSupportedFormats& supported_formats) {
  auto& sf = supported_formats.sample_formats;
  if (std::find(sf.begin(), sf.end(), sample_format) == sf.end()) {
    return false;
  }
  auto& bps = supported_formats.bytes_per_sample;
  if (std::find(bps.begin(), bps.end(), bytes_per_sample) == bps.end()) {
    return false;
  }
  auto& vbps = supported_formats.valid_bits_per_sample;
  if (std::find(vbps.begin(), vbps.end(), valid_bits_per_sample) == vbps.end()) {
    return false;
  }
  return true;
}

bool IsNumberOfChannelsInSupported(uint32_t number_of_channels,
                                   const fuchsia::hardware::audio::PcmSupportedFormats& format) {
  for (auto channels : format.number_of_channels) {
    if (channels == number_of_channels) {
      return true;
    }
  }
  return false;
}

bool IsRateInSupported(uint32_t frame_rate,
                       const fuchsia::hardware::audio::PcmSupportedFormats& format) {
  for (auto rate : format.frame_rates) {
    if (rate == frame_rate) {
      return true;
    }
  }
  return false;
}

bool IsFormatInSupported(
    const fuchsia::media::AudioStreamType& stream_type,
    const std::vector<fuchsia::hardware::audio::PcmSupportedFormats>& supported_formats) {
  DriverSampleFormat driver_format = {};
  if (!AudioSampleFormatToDriverSampleFormat(stream_type.sample_format, &driver_format)) {
    return false;
  }

  // Is there a match for any given suppored format where we find sample format, number of channels
  // and rate.
  for (const auto& format : supported_formats) {
    if (IsSampleFormatInSupported(driver_format.sample_format, driver_format.bytes_per_sample,
                                  driver_format.valid_bits_per_sample, format) &&
        IsNumberOfChannelsInSupported(stream_type.channels, format) &&
        IsRateInSupported(stream_type.frames_per_second, format)) {
      return true;
    }
  }
  return false;
}

zx_status_t SelectBestFormat(const std::vector<fuchsia::hardware::audio::PcmSupportedFormats>& fmts,
                             uint32_t* frames_per_second_inout, uint32_t* channels_inout,
                             fuchsia::media::AudioSampleFormat* sample_format_inout) {
  TRACE_DURATION("audio", "SelectBestFormat");
  if ((frames_per_second_inout == nullptr) || (channels_inout == nullptr) ||
      (sample_format_inout == nullptr)) {
    return ZX_ERR_INVALID_ARGS;
  }
  uint32_t pref_frame_rate = *frames_per_second_inout;
  uint32_t pref_channels = *channels_inout;

  DriverSampleFormat pref_sample_format = {};
  // Only valid pref_sample_formats are: unsigned-8, signed-16, signed-24in32 or float-32.
  if (!AudioSampleFormatToDriverSampleFormat(*sample_format_inout, &pref_sample_format)) {
    FX_LOGS(ERROR) << "Failed to convert FIDL sample format ("
                   << static_cast<uint32_t>(*sample_format_inout) << ") to driver sample format.";
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t best_frame_rate = 0;
  uint32_t best_channels = 0;
  DriverSampleFormat best_sample_format = {};
  uint32_t best_score = 0;
  uint32_t best_frame_rate_delta = std::numeric_limits<uint32_t>::max();

  for (const auto& format : fmts) {
    // Start by scoring our sample format. Right now, the audio core supports 8-bit unsigned, 16-bit
    // signed, 24-bit-in-32 signed and 32-bit float. If this sample format range does not support
    // any of these, just skip it for now. Otherwise, 5 points if you match the requested format, 4
    // for signed-24, 3 for signed-16, 2 for float-32, or 1 for unsigned-8.
    DriverSampleFormat this_sample_format;
    int sample_format_score = 0;

    // Check for direct match.
    if (IsSampleFormatInSupported(pref_sample_format.sample_format,
                                  pref_sample_format.bytes_per_sample,
                                  pref_sample_format.valid_bits_per_sample, format)) {
      this_sample_format = pref_sample_format;
      sample_format_score = 5;
    } else if (IsSampleFormatInSupported(fuchsia::hardware::audio::SampleFormat::PCM_SIGNED, 4, 24,
                                         format)) {
      this_sample_format = {fuchsia::hardware::audio::SampleFormat::PCM_SIGNED, 4, 24};
      sample_format_score = 4;
    } else if (IsSampleFormatInSupported(fuchsia::hardware::audio::SampleFormat::PCM_SIGNED, 2, 16,
                                         format)) {
      this_sample_format = {fuchsia::hardware::audio::SampleFormat::PCM_SIGNED, 2, 16};
      sample_format_score = 3;
    } else if (IsSampleFormatInSupported(fuchsia::hardware::audio::SampleFormat::PCM_FLOAT, 4, 32,
                                         format)) {
      this_sample_format = {fuchsia::hardware::audio::SampleFormat::PCM_FLOAT, 4, 32};
      sample_format_score = 2;
    } else if (IsSampleFormatInSupported(fuchsia::hardware::audio::SampleFormat::PCM_UNSIGNED, 1, 8,
                                         format)) {
      this_sample_format = {fuchsia::hardware::audio::SampleFormat::PCM_UNSIGNED, 1, 8};
      sample_format_score = 1;
    }

    // Next consider the supported channel counts. 3 points for matching the requested channel
    // count. Otherwise, default to stereo (if supported) and score 2 points. Failing that, just
    // pick the top end of the supported channel range and score 1 point.
    uint32_t this_channels = 0;
    int channel_count_score = 0;

    if (IsNumberOfChannelsInSupported(pref_channels, format)) {
      this_channels = pref_channels;
      channel_count_score = 3;
    } else if (IsNumberOfChannelsInSupported(2, format)) {
      this_channels = 2;
      channel_count_score = 2;
    } else {
      this_channels =
          *std::max_element(format.number_of_channels.begin(), format.number_of_channels.end());
      channel_count_score = 1;
    }

    // Next score based on supported frame rates. Score 3 points for a match, 2 points if we have to
    // scale up to the nearest supported rate, or 1 point if we have to scale down.
    //
    uint32_t this_frame_rate = 0;
    uint32_t frame_rate_delta = std::numeric_limits<uint32_t>::max();
    int frame_rate_score = 0;

    if (IsRateInSupported(pref_frame_rate, format)) {
      this_frame_rate = pref_frame_rate;
      channel_count_score = 3;
      frame_rate_delta = 0;
    } else {
      uint32_t delta = std::numeric_limits<uint32_t>::max();
      for (auto& i : format.frame_rates) {
        if (std::abs((long)i - (long)pref_frame_rate) < delta) {
          delta = std::abs((long)i - (long)pref_frame_rate);
          this_frame_rate = i;
        }
      }
      if (pref_frame_rate < this_frame_rate) {
        frame_rate_delta = this_frame_rate - pref_frame_rate;
        channel_count_score = 2;
      } else {
        frame_rate_delta = pref_frame_rate - this_frame_rate;
        channel_count_score = 1;
      }
    }

    // OK, we have computed the best option supported by this frame rate range. Weight the score,
    // and it if it better then any of our previous best score, replace our previous best with this.
    uint32_t score;
    score = (sample_format_score * 100)   // format is the most important.
            + (channel_count_score * 10)  // channel count comes second.
            + frame_rate_score;           // frame rate is the least important.

    FX_DCHECK(score > 0);

    // If this score is better than the current best score, or this score ties the current best
    // score but the frame rate distance is less, then this is the new best format.
    if ((score > best_score) ||
        ((score == best_score) && (frame_rate_delta < best_frame_rate_delta))) {
      best_frame_rate = this_frame_rate;
      best_frame_rate_delta = frame_rate_delta;
      best_channels = this_channels;
      best_sample_format = this_sample_format;
      best_score = score;
    }
  }

  // If our score is still zero, then there must have be absolutely no supported formats in the set
  // provided by the driver.
  if (!best_score) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  __UNUSED bool convert_res =
      DriverSampleFormatToAudioSampleFormat(best_sample_format, sample_format_inout);
  FX_DCHECK(convert_res);

  *channels_inout = best_channels;
  *frames_per_second_inout = best_frame_rate;

  return ZX_OK;
}

zx_status_t SelectBestFormat(const std::vector<audio_stream_format_range_t>& fmts,
                             uint32_t* frames_per_second_inout, uint32_t* channels_inout,
                             fuchsia::media::AudioSampleFormat* sample_format_inout) {
  TRACE_DURATION("audio", "SelectBestFormat");
  if ((frames_per_second_inout == nullptr) || (channels_inout == nullptr) ||
      (sample_format_inout == nullptr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t pref_frame_rate = *frames_per_second_inout;
  uint32_t pref_channels = *channels_inout;
  audio_sample_format_t pref_sample_format;

  if (!AudioSampleFormatToDriverSampleFormat(*sample_format_inout, &pref_sample_format)) {
    FX_LOGS(WARNING) << "Failed to convert FIDL sample format ("
                     << static_cast<uint32_t>(*sample_format_inout) << ") to driver sample format.";
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t best_frame_rate;
  uint32_t best_channels;
  audio_sample_format_t best_sample_format;
  uint32_t best_score = 0;
  uint32_t best_frame_rate_delta = std::numeric_limits<uint32_t>::max();

  constexpr uint32_t U8_FMT = AUDIO_SAMPLE_FORMAT_8BIT | AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED;
  constexpr uint32_t S16_FMT = AUDIO_SAMPLE_FORMAT_16BIT;
  constexpr uint32_t S24_FMT = AUDIO_SAMPLE_FORMAT_24BIT_IN32;
  constexpr uint32_t F32_FMT = AUDIO_SAMPLE_FORMAT_32BIT_FLOAT;

  // Users should only ask for unsigned-8, signed-16, signed-24in32 or float-32. If they ask for
  // anything else, change their preference to signed-16.
  //
  // TODO(johngro) : clean this up as part of fixing fxbug.dev/13341
  if ((pref_sample_format & AUDIO_SAMPLE_FORMAT_FLAG_INVERT_ENDIAN) ||
      (((pref_sample_format & U8_FMT) != U8_FMT) && ((pref_sample_format & S16_FMT) != S16_FMT) &&
       ((pref_sample_format & S24_FMT) != S24_FMT) &&
       ((pref_sample_format & F32_FMT) != F32_FMT))) {
    pref_sample_format = AUDIO_SAMPLE_FORMAT_16BIT;
  }

  for (const auto& range : fmts) {
    // Start by scoring our sample format. Right now, the audio core supports 8-bit unsigned, 16-bit
    // signed, 24-bit-in-32 signed and 32-bit float. If this sample format range does not support
    // any of these, just skip it for now. Otherwise, 5 points if you match the requested format, 4
    // for signed-24, 3 for signed-16, 2 for float-32, or 1 for unsigned-8.
    audio_sample_format_t this_sample_format;
    int sample_format_score;

    bool supports_u8 = (range.sample_formats & U8_FMT) == U8_FMT;
    bool supports_s16 = (range.sample_formats & S16_FMT) == S16_FMT;
    bool supports_s24 = (range.sample_formats & S24_FMT) == S24_FMT;
    bool supports_f32 = (range.sample_formats & F32_FMT) == F32_FMT;
    if ((range.sample_formats & AUDIO_SAMPLE_FORMAT_FLAG_INVERT_ENDIAN) ||
        (!supports_u8 && !supports_s16 && !supports_s24 && !supports_f32)) {
      continue;  // Otherwise skip: this isn't a sample container we understand.
    }

    if ((pref_sample_format & range.sample_formats) == pref_sample_format) {
      // Direct match.
      this_sample_format = pref_sample_format;
      sample_format_score = 5;
    } else if (supports_s24) {
      this_sample_format = AUDIO_SAMPLE_FORMAT_24BIT_IN32;
      sample_format_score = 4;
    } else if (supports_s16) {
      this_sample_format = AUDIO_SAMPLE_FORMAT_16BIT;
      sample_format_score = 3;
    } else if (supports_f32) {
      this_sample_format = AUDIO_SAMPLE_FORMAT_32BIT_FLOAT;
      sample_format_score = 2;
    } else {
      FX_DCHECK(supports_u8);
      this_sample_format = static_cast<audio_sample_format_t>(U8_FMT);
      sample_format_score = 1;
    }

    // Next consider the supported channel counts. 3 points for matching the requested channel
    // count. Otherwise, default to stereo (if supported) and score 2 points. Failing that, just
    // pick the top end of the supported channel range and score 1 point.
    uint32_t this_channels;
    int channel_count_score;
    if ((pref_channels >= range.min_channels) && (pref_channels <= range.max_channels)) {
      this_channels = pref_channels;
      channel_count_score = 3;
    } else if ((2 >= range.min_channels) && (2 <= range.max_channels)) {
      this_channels = 2;
      channel_count_score = 2;
    } else {
      this_channels = range.max_channels;
      channel_count_score = 1;
    }

    // Next score based on supported frame rates. Score 3 points for a match, 2 points if we have to
    // scale up to the nearest supported rate, or 1 point if we have to scale down.
    //
    if (range.min_frames_per_second > range.max_frames_per_second) {
      // Skip this frame rate range entirely if it is empty.
      FX_LOGS(INFO) << "Skipping empty frame rate range [" << range.min_frames_per_second << ", "
                    << range.max_frames_per_second
                    << "] while searching for best format in driver list.";
      continue;
    }

    uint32_t this_frame_rate = 0;
    uint32_t frame_rate_delta = std::numeric_limits<uint32_t>::max();
    int frame_rate_score = 0;
    if (range.flags & ASF_RANGE_FLAG_FPS_CONTINUOUS) {
      // This is a continuous sample rate range. If we are within the range, thats a match.
      // Otherwise move up/down as needed to match the min/max of the range as appropriate.
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
        FX_DCHECK(pref_frame_rate > range.max_frames_per_second);
        this_frame_rate = range.max_frames_per_second;
        frame_rate_score = 1;
        frame_rate_delta = pref_frame_rate - range.max_frames_per_second;
      }
    } else {
      // This is a discrete sample rate range. Use the frame rate enumerator utility class to
      // evaluate each of the possible frame rates.
      for (const uint32_t rate : ::audio::utils::FrameRateEnumerator(range)) {
        if (pref_frame_rate == rate) {
          // We matched our preference. No need to keep searching.
          this_frame_rate = rate;
          frame_rate_score = 3;
          frame_rate_delta = 0;
          break;
        }

        if (pref_frame_rate < rate) {
          // Scaling up; 2 points.  If this better than what we were doing before, just choose it.
          // If we were already going to scale up, pick the frame rate which is closer to our
          // preference. Otherwise, do nothing.
          if ((frame_rate_score < 2) || ((frame_rate_score == 2) && (rate < this_frame_rate))) {
            this_frame_rate = rate;
            frame_rate_score = 2;
            frame_rate_delta = rate - pref_frame_rate;
          }
        } else {
          FX_DCHECK(pref_frame_rate > rate);

          // Scaling down; 1 point.  If this better than what we were doing before, just choose it.
          // If we were already going to scale down, pick the frame rate which is closer to our
          // preference. Otherwise, do nothing.
          if ((frame_rate_score < 1) || ((frame_rate_score == 1) && (rate > this_frame_rate))) {
            this_frame_rate = rate;
            frame_rate_score = 1;
            frame_rate_delta = pref_frame_rate - rate;
          }
        }
      }
    }

    // If our frame rate score is still zero, it means that we were given a discrete frame range by
    // a driver which was completely empty (even though min was <= max as it should be)  Debug log a
    // warning, then skip the range entirely.
    if (frame_rate_score == 0) {
      FX_LOGS(INFO) << "Skipping empty discrete frame rate range [" << range.min_frames_per_second
                    << ", " << range.max_frames_per_second << "] (flags " << range.flags
                    << ") while searching for best format";
      continue;
    }

    // OK, we have computed the best option supported by this frame rate range. Weight the score,
    // and it if it better then any of our previous best score, replace our previous best with this.
    uint32_t score;
    score = (sample_format_score * 100)   // format is the most important.
            + (channel_count_score * 10)  // channel count comes second.
            + frame_rate_score;           // frame rate is the least important.

    FX_DCHECK(score > 0);
    FX_DCHECK(::audio::utils::FormatIsCompatible(this_frame_rate, this_channels, this_sample_format,
                                                 range));

    // If this score is better than the current best score, or this score ties the current best
    // score but the frame rate distance is less, then this is the new best format.
    if ((score > best_score) ||
        ((score == best_score) && (frame_rate_delta < best_frame_rate_delta))) {
      best_frame_rate = this_frame_rate;
      best_frame_rate_delta = frame_rate_delta;
      best_channels = this_channels;
      best_sample_format = this_sample_format;
      best_score = score;
    }
  }

  // If our score is still zero, then there must have be absolutely no supported formats in the set
  // provided by the driver.
  if (!best_score) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  __UNUSED bool convert_res =
      DriverSampleFormatToAudioSampleFormat(best_sample_format, sample_format_inout);
  FX_DCHECK(convert_res);

  *channels_inout = best_channels;
  *frames_per_second_inout = best_frame_rate;

  return ZX_OK;
}

zx_status_t AcquireHighPriorityProfile(zx::profile* profile) {
  TRACE_DURATION("audio", "AcquireHighPriorityProfile");
  // Use threadsafe static initialization to get our one-and-only copy of this profile object. Each
  // subsequent call will return a duplicate of that profile handle to ensure sharing of thread
  // pools.
  static zx::profile high_priority_profile;
  static zx_status_t initial_status = [](zx::profile* profile) {
    zx::channel ch0, ch1;
    zx_status_t res = zx::channel::create(0u, &ch0, &ch1);
    if (res != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to create channel, res=" << res;
      return res;
    }

    res = fdio_service_connect(
        (std::string("/svc/") + fuchsia::scheduler::ProfileProvider::Name_).c_str(), ch0.release());
    if (res != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to connect to ProfileProvider, res=" << res;
      return res;
    }

    fuchsia::scheduler::ProfileProvider_SyncProxy provider(std::move(ch1));

    zx_status_t fidl_status;
    zx::profile res_profile;
    res = provider.GetDeadlineProfile(ThreadingModel::kMixProfileCapacity.get(),
                                      ThreadingModel::kMixProfileDeadline.get(),
                                      ThreadingModel::kMixProfilePeriod.get(),
                                      "src/media/audio/audio_core", &fidl_status, &res_profile);
    if (res != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to create profile, res=" << res;
      return res;
    }
    if (fidl_status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to create profile, fidl_status=" << fidl_status;
      return fidl_status;
    }

    *profile = std::move(res_profile);
    return ZX_OK;
  }(&high_priority_profile);

  // If the initial acquisition of the profile failed, return that status.
  if (initial_status != ZX_OK)
    return initial_status;

  // Otherwise, dupe this handle and return it.
  return high_priority_profile.duplicate(ZX_RIGHT_SAME_RIGHTS, profile);
}

void AcquireRelativePriorityProfile(uint32_t priority, sys::ComponentContext* context,
                                    fit::function<void(zx::profile)> callback) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("audio", "AcquireRelativePriorityProfile");
  TRACE_FLOW_BEGIN("audio", "GetProfile", nonce);
  auto profile_provider = context->svc()->Connect<fuchsia::scheduler::ProfileProvider>();
  profile_provider->GetProfile(
      priority, "src/media/audio/audio_core/audio_core_impl",
      // Note we move the FIDL ptr into the closure to ensure we keep the channel open until we
      // receive the callback, otherwise it will be impossible to get a response.
      [profile_provider = std::move(profile_provider), callback = std::move(callback), nonce](
          zx_status_t status, zx::profile profile) {
        TRACE_DURATION("audio", "GetProfile callback");
        TRACE_FLOW_END("audio", "GetProfile", nonce);
        if (status == ZX_OK) {
          callback(std::move(profile));
        } else {
          callback(zx::profile());
        }
      });
}

void AcquireAudioCoreImplProfile(sys::ComponentContext* context,
                                 fit::function<void(zx::profile)> callback) {
  AcquireRelativePriorityProfile(/* HIGH_PRIORITY in zircon */ 24, context, std::move(callback));
}

}  // namespace media::audio
