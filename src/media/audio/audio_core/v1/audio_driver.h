// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_AUDIO_DRIVER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_AUDIO_DRIVER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>
#include <lib/zx/clock.h>
#include <lib/zx/vmo.h>
#include <zircon/device/audio.h>

#include <mutex>
#include <string>

#include "src/media/audio/audio_core/shared/reporter.h"
#include "src/media/audio/audio_core/v1/audio_device.h"
#include "src/media/audio/audio_core/v1/audio_device_settings.h"
#include "src/media/audio/audio_core/v1/channel_attributes.h"
#include "src/media/audio/audio_core/v1/ring_buffer.h"
#include "src/media/audio/audio_core/v1/utils.h"

namespace media::audio {

class AudioOutput;

struct HwGainState {
  // TODO(johngro): when driver interfaces move to FIDL, just change this to match the fidl
  // structure returned from a GetGain request by the driver.
  bool cur_mute;
  bool cur_agc;
  float cur_gain;

  bool can_mute;
  bool can_agc;
  float min_gain;
  float max_gain;
  float gain_step;
};

class AudioDriver {
 public:
  // Timeout values are chosen to be generous while still providing some guard-rails against
  // hardware errors. Correctly functioning hardware and drivers should never result in any
  // timeouts.
  static constexpr zx::duration kDefaultShortCmdTimeout = zx::sec(2);
  static constexpr zx::duration kDefaultLongCmdTimeout = zx::sec(4);

  enum class State {
    Uninitialized,
    MissingDriverInfo,
    Unconfigured,
    Configuring_SettingFormat,
    Configuring_GettingFifoDepth,
    Configuring_GettingRingBuffer,
    Configured,
    Starting,
    Started,
    Stopping,
    Shutdown,
  };

  explicit AudioDriver(AudioDevice* owner);

  using DriverTimeoutHandler = fit::function<void(zx::duration)>;
  AudioDriver(AudioDevice* owner, DriverTimeoutHandler timeout_handler);

  virtual ~AudioDriver() = default;

  zx_status_t Init(zx::channel stream_channel);
  void Cleanup();
  std::optional<Format> GetFormat() const;

  bool plugged() const {
    std::lock_guard<std::mutex> lock(plugged_lock_);
    return plugged_;
  }

  zx::time plug_time() const {
    std::lock_guard<std::mutex> lock(plugged_lock_);
    return plug_time_;
  }

  // Methods which need to be called from the owner's execution domain.  If there was a good way to
  // use the static lock analysis to ensure this, I would do so, but unfortunately the compiler is
  // unable to figure out that the owner calling these methods is always the same as owner_.
  State state() const { return state_; }
  zx::time ref_start_time() const { return ref_start_time_; }
  zx::duration external_delay() const { return external_delay_; }
  uint32_t fifo_depth_frames() const { return fifo_depth_frames_; }
  zx::duration fifo_depth_duration() const { return fifo_depth_duration_; }
  zx_koid_t stream_channel_koid() const { return stream_channel_koid_; }
  const HwGainState& hw_gain_state() const { return hw_gain_state_; }
  uint32_t clock_domain() const { return clock_domain_; }

  virtual const TimelineFunction& ref_time_to_frac_presentation_frame() const {
    return ref_time_to_frac_presentation_frame_;
  }
  virtual const TimelineFunction& ref_time_to_frac_safe_read_or_write_frame() const {
    return ref_time_to_frac_safe_read_or_write_frame_;
  }

  // The following properties are only safe to access after the driver is beyond the
  // MissingDriverInfo state.  After that state, these members must be treated as immutable, and the
  // driver class may no longer change them.
  const audio_stream_unique_id_t& persistent_unique_id() const { return persistent_unique_id_; }
  const std::string& manufacturer_name() const { return manufacturer_name_; }
  const std::string& product_name() const { return product_name_; }

  zx_status_t GetDriverInfo();
  zx_status_t Configure(const Format& format, zx::duration min_ring_buffer_duration);
  zx_status_t Start();
  zx_status_t Stop();
  zx_status_t SetPlugDetectEnabled(bool enabled);
  zx_status_t SetGain(const AudioDeviceSettings::GainState& gain_state,
                      audio_set_gain_flags_t set_flags);
  zx_status_t SelectBestFormat(uint32_t* frames_per_second_inout, uint32_t* channels_inout,
                               fuchsia::media::AudioSampleFormat* sample_format_inout);

  // Accessors for the ring buffer pointer and the current output clock transformation.
  //
  // Note: Only AudioDriver writes to these, and only when in our owner's mixing execution
  // domain.  It is safe for our owner to read these objects, but only when operating in the mixing
  // domain.  Unfortunately, it is not practical to use the static thread safety annotation to prove
  // that we are accessing these variable from the mixing domain.  Instead, we...
  //
  // 1) Make these methods private.
  // 2) Make the AudioDevice class (our owner) a friend.
  // 3) Expose protected accessors in AudioDevice which demand that we execute in the mix domain.
  //
  // This should be a strong enough guarantee to warrant disabling the thread safety analysis here.
  const std::shared_ptr<ReadableRingBuffer>& readable_ring_buffer() const
      FXL_NO_THREAD_SAFETY_ANALYSIS {
    return readable_ring_buffer_;
  }
  const std::shared_ptr<WritableRingBuffer>& writable_ring_buffer() const
      FXL_NO_THREAD_SAFETY_ANALYSIS {
    return writable_ring_buffer_;
  }

  std::shared_ptr<Clock> reference_clock() { return audio_clock_; }
  zx::duration turn_on_delay() { return turn_on_delay_; }
  std::vector<ChannelAttributes> channel_config() { return configured_channel_config_; }

  zx_status_t SetActiveChannels(uint64_t chan_bit_mask);

  Reporter::AudioDriverInfo info_for_reporter() const;

 private:
  static bool ValidatePcmSupportedFormats(
      std::vector<fuchsia::hardware::audio::PcmSupportedFormats>& formats, bool is_input);

  static constexpr uint32_t kDriverInfoHasUniqueId = (1u << 0);
  static constexpr uint32_t kDriverInfoHasMfrStr = (1u << 1);
  static constexpr uint32_t kDriverInfoHasProdStr = (1u << 2);
  static constexpr uint32_t kDriverInfoHasGainState = (1u << 3);
  static constexpr uint32_t kDriverInfoHasFormats = (1u << 4);
  static constexpr uint32_t kDriverInfoHasClockDomain = (1u << 5);
  static constexpr uint32_t kDriverInfoHasAll = kDriverInfoHasUniqueId | kDriverInfoHasMfrStr |
                                                kDriverInfoHasProdStr | kDriverInfoHasGainState |
                                                kDriverInfoHasFormats | kDriverInfoHasClockDomain;

  void SetUpClocks();
  void ClockRecoveryUpdate(fuchsia::hardware::audio::RingBufferPositionInfo info);

  zx_status_t SetGain(const AudioDeviceSettings::GainState& gain_state);
  // Transition to the Shutdown state and begin the process of shutting down.
  void ShutdownSelf(const char* debug_reason = nullptr, zx_status_t debug_status = ZX_OK)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());

  // Evaluate each currently pending timeout. Program the command timeout timer appropriately.
  void SetupCommandTimeout() FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());

  // Update internal plug state bookkeeping and report up to our owner (if enabled).
  void ReportPlugStateChange(bool plugged, zx::time plug_time)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());

  // Handle a new piece of driver info being fetched.
  zx_status_t OnDriverInfoFetched(uint32_t info)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());

  // Simple accessors
  bool operational() const FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token()) {
    return (state_ != State::Uninitialized) && (state_ != State::Shutdown);
  }

  bool fetching_driver_info() const FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token()) {
    return fetch_driver_info_deadline_ != zx::time::infinite();
  }

  void DriverCommandTimedOut() FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());
  void RequestNextPlugStateChange() FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());
  void RequestNextClockRecoveryUpdate() FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());

  AudioDevice* const owner_;
  DriverTimeoutHandler timeout_handler_;

  State state_ = State::Uninitialized;

  async::TaskClosure cmd_timeout_ FXL_GUARDED_BY(owner_->mix_domain().token());

  zx_koid_t stream_channel_koid_ = ZX_KOID_INVALID;
  zx::time fetch_driver_info_deadline_ = zx::time::infinite();
  uint32_t fetched_driver_info_ FXL_GUARDED_BY(owner_->mix_domain().token()) = 0;

  // State fetched at driver startup time.
  audio_stream_unique_id_t persistent_unique_id_ = {0};
  std::string manufacturer_name_;
  std::string product_name_;
  HwGainState hw_gain_state_;
  std::vector<audio_stream_format_range_t> format_ranges_;

  // Configuration state.
  zx::time mono_start_time_;
  zx::time ref_start_time_;
  zx::duration external_delay_;
  zx::duration min_ring_buffer_duration_;
  uint32_t fifo_depth_frames_;
  zx::duration fifo_depth_duration_;
  zx::duration turn_on_delay_ = zx::nsec(0);
  zx::time configuration_deadline_ = zx::time::infinite();

  // A stashed copy of current format, queryable by destinations (outputs or AudioCapturers) when
  // determining which mixer to use.
  mutable std::mutex configured_format_lock_;
  std::optional<Format> configured_format_ FXL_GUARDED_BY(configured_format_lock_);
  std::vector<ChannelAttributes> configured_channel_config_;

  // Ring buffer state. Details are lock-protected and changes tracked with generation counter,
  // allowing AudioCapturer clients to snapshot ring-buffer state during mix/resample operations.
  mutable std::mutex ring_buffer_state_lock_;
  std::shared_ptr<ReadableRingBuffer> readable_ring_buffer_ FXL_GUARDED_BY(ring_buffer_state_lock_);
  std::shared_ptr<WritableRingBuffer> writable_ring_buffer_ FXL_GUARDED_BY(ring_buffer_state_lock_);

  // The timeline function which maps from either capture time (Input) or presentation time
  // (Output) at speaker/microphone on the audio device's ref clock, to stream's subframe position.
  //
  // IOW - given a stream's frame number, use the inverse of this function to map to a time on
  // device ref clock that the frame [was captured / will be presented].
  fbl::RefPtr<VersionedTimelineFunction> versioned_ref_time_to_frac_presentation_frame_;

  // Useful timeline functions which are computed after streaming starts.  See
  // the comments for the accessors in audio_device.h for detailed descriptions.
  TimelineFunction ref_time_to_frac_presentation_frame_
      FXL_GUARDED_BY(owner_->mix_domain().token());
  TimelineFunction ref_time_to_frac_safe_read_or_write_frame_
      FXL_GUARDED_BY(owner_->mix_domain().token());
  TimelineRate frac_frames_per_byte_ FXL_GUARDED_BY(owner_->mix_domain().token());

  mutable std::mutex plugged_lock_;
  bool plugged_ FXL_GUARDED_BY(plugged_lock_) = false;
  zx::time plug_time_ FXL_GUARDED_BY(plugged_lock_);

  zx::time driver_last_timeout_ = zx::time::infinite();

  // Plug detection state.
  bool pd_hardwired_ = false;

  std::vector<fuchsia::hardware::audio::PcmSupportedFormats> formats_;

  // FIDL interface pointers.
  fidl::InterfacePtr<fuchsia::hardware::audio::StreamConfig> stream_config_fidl_;
  fidl::InterfacePtr<fuchsia::hardware::audio::RingBuffer> ring_buffer_fidl_;

  uint32_t clock_domain_ = Clock::kMonotonicDomain;
  std::shared_ptr<Clock> audio_clock_;
  std::shared_ptr<RecoveredClock> recovered_clock_;

  // Counter of received position notifications since START.
  uint64_t position_notification_count_ FXL_GUARDED_BY(owner_->mix_domain().token()) = 0;
  uint64_t ring_buffer_size_bytes_ FXL_GUARDED_BY(owner_->mix_domain().token());
  uint64_t running_pos_bytes_ FXL_GUARDED_BY(owner_->mix_domain().token());

  // If we get an error from ring_buffer_fidl->SetActiveChannels(), then we won't call it again
  zx_status_t set_active_channels_err_ = ZX_OK;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_AUDIO_DRIVER_H_
