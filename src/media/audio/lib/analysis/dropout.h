// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_ANALYSIS_DROPOUT_H_
#define SRC_MEDIA_AUDIO_LIB_ANALYSIS_DROPOUT_H_

#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

#include <cmath>
#include <iomanip>
#include <limits>
#include <optional>

#include "src/media/audio/lib/format/audio_buffer.h"

namespace media::audio {

// Below are two utility classes that can be used to detect dropouts.

// PowerChecker verifies that a stream contains a signal of expected power.
// It calculates an audio section's power, returning false if this does not meet the expected val.
//
// This checker includes each sample in exactly one calculation (it does not use overlapping RMS
// windows). E.g., for a 512-sample window, the first RMS check is based on samples 0-511, and the
// second check based on samples 512-1023 (modulo any intervening RestartRmsWindow() calls).
//
// For simplicity, this class is currently limited to FLOAT data only.
class PowerChecker {
 public:
  PowerChecker(int64_t rms_window_in_frames, int32_t channels, double expected_min_power_rms,
               const std::string& tag = "")
      : rms_window_in_frames_(rms_window_in_frames),
        channels_(channels),
        expected_power_rms_(expected_min_power_rms),
        tag_(tag.empty() ? tag : tag + ": ") {
    RestartRmsWindow();
  }

  bool Check(const float* samples, int64_t frame_position, int64_t num_frames, bool print = false) {
    if (frame_position_ != frame_position) {
      RestartRmsWindow();
    }
    frame_position_ = frame_position + num_frames;

    bool pass = true;
    // Ingest all the provided samples before leaving
    while (num_frames) {
      // Starting from any previously-retained running totals/counts, incorporate each additional
      // sample until we have enough to analyze. Stop earlier if we run out of samples.
      while (running_window_frame_count_ < rms_window_in_frames_ && num_frames) {
        for (auto chan = 0; chan < channels_; ++chan) {
          auto val = *samples;
          running_sum_squares_ += (val * val);
          samples++;
        }
        running_window_frame_count_++;
        num_frames--;
      }
      // If we have enough to analyze, do so now and reset our running totals/counts to zero.
      // Otherwise, just ingest these values and return true.
      if (running_window_frame_count_ == rms_window_in_frames_) {
        auto mean_squares =
            running_sum_squares_ / static_cast<double>(rms_window_in_frames_ * channels_);
        auto current_root_mean_squares = sqrt(mean_squares);
        if (current_root_mean_squares + std::numeric_limits<float>::epsilon() <
            expected_power_rms_) {
          // We might have been provided enough samples for multiple windows (thus more than one
          // success/fail calculation). If ANY of them fail, return false.
          pass = false;
          if (print) {
            FX_LOGS(ERROR) << tag_ << "********** Dropout detected -- measured power " << std::fixed
                           << std::setprecision(6) << std::setw(8) << current_root_mean_squares
                           << " (expected " << std::fixed << std::setprecision(4) << std::setw(6)
                           << expected_power_rms_ << ") across window of " << rms_window_in_frames_
                           << " frames **********";
          }
        } else {
          if constexpr (kSuccessLogStride) {
            if (print) {
              if (success_log_count_ == 0) {
                FX_LOGS(INFO) << tag_ << "********** Across window of " << rms_window_in_frames_
                              << " frames, successfully measured power " << std::fixed
                              << std::setprecision(6) << std::setw(8) << current_root_mean_squares
                              << " (expected " << std::fixed << std::setprecision(4) << std::setw(6)
                              << expected_power_rms_ << ") **********";
              }
              success_log_count_ = ++success_log_count_ % kSuccessLogStride;
            }
          }
        }
        RestartRmsWindow();
      }
    }
    return pass;
  }

 private:
  void RestartRmsWindow() {
    running_window_frame_count_ = 0;
    running_sum_squares_ = 0.0;
  }

  const int64_t rms_window_in_frames_;
  const int32_t channels_;  // only used for display purposes
  const double expected_power_rms_;
  const std::string tag_;

  int64_t frame_position_ = 0;
  int64_t running_window_frame_count_;
  double running_sum_squares_;

  // To calibrate appropriate RMS limits for specific content, display the calculated RMS power even
  // on success. A stride reduces log spam; making it PRIME (797 is appropriate) varies sampling
  // across periodic signals. To disable this logging altogether, set kSuccessLogStride to 0.
  static constexpr int64_t kSuccessLogStride = 0;
  int64_t success_log_count_ = 0;  // only used for display purposes
};

// SilenceChecker verifies that a stream does not contain a consecutive number of truly silent
// frames, with Check() returning false if this ever occurs. For simplicity, this class is currently
// limited to FLOAT data only.
class SilenceChecker {
 public:
  explicit SilenceChecker(int64_t max_count_silent_frames_allowed, int32_t channels,
                          const std::string& tag = "")
      : max_silent_frames_allowed_(max_count_silent_frames_allowed),
        channels_(channels),
        tag_(tag.empty() ? tag : tag + ": ") {}

  bool Check(const float* samples, int64_t frame_position, int64_t num_frames, bool print = false) {
    if (frame_position_ != frame_position) {
      running_silent_frame_count_ = 0;
    }
    frame_position_ = frame_position + num_frames;

    int64_t max_silent_frames_detected = 0;

    // Ingest all provided samples before leaving
    while (num_frames--) {
      // Starting from any previously-retained running count, incorporate additional silent frames.
      bool frame_is_silent = true;
      for (auto chan = 0; chan < channels_; ++chan) {
        if (samples[chan] > std::numeric_limits<float>::epsilon() ||
            samples[chan] < -std::numeric_limits<float>::epsilon()) {
          frame_is_silent = false;
          break;
        }
      }
      running_silent_frame_count_ = frame_is_silent ? running_silent_frame_count_ + 1 : 0;
      max_silent_frames_detected =
          std::max(max_silent_frames_detected, running_silent_frame_count_);
      samples += channels_;
    }
    if (print && max_silent_frames_detected > max_silent_frames_allowed_) {
      FX_LOGS(ERROR) << tag_ << "********* Silence detected -- measured "
                     << max_silent_frames_detected
                     << " consecutive silent frames (max allowed: " << max_silent_frames_allowed_
                     << ") **********";
    }
    return (max_silent_frames_detected <= max_silent_frames_allowed_);
  }

 private:
  const int64_t max_silent_frames_allowed_;
  const int32_t channels_;
  const std::string tag_;

  int64_t frame_position_ = 0;
  int64_t running_silent_frame_count_ = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_ANALYSIS_DROPOUT_H_
