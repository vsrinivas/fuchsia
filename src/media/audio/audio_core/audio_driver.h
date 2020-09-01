// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DRIVER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DRIVER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>
#include <lib/zx/clock.h>
#include <lib/zx/vmo.h>
#include <zircon/device/audio.h>

#include <mutex>
#include <string>

#include "src/media/audio/audio_core/audio_device.h"
#include "src/media/audio/audio_core/audio_device_settings.h"
#include "src/media/audio/audio_core/ring_buffer.h"
#include "src/media/audio/audio_core/utils.h"

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

enum class AudioDriverVersion : uint8_t {
  V1,  // Legacy, manual serialization in system/public/zircon/device/audio.h.
  V2,  // FIDL, defined in sdk/fidl/fuchsia.hardware.audio.
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

  AudioDriver() = default;
  virtual ~AudioDriver() = default;

  virtual zx_status_t Init(zx::channel stream_channel) = 0;
  virtual void Cleanup() = 0;
  virtual std::optional<Format> GetFormat() const = 0;
  virtual bool plugged() const = 0;
  virtual zx::time plug_time() const = 0;

  // Methods which need to be called from the owner's execution domain.  If there was a good way to
  // use the static lock analysis to ensure this, I would do so, but unfortunately the compiler is
  // unable to figure out that the owner calling these methods is always the same as owner_.
  virtual State state() const = 0;
  virtual zx::time ref_start_time() const = 0;
  virtual zx::duration external_delay() const = 0;
  virtual uint32_t fifo_depth_frames() const = 0;
  virtual zx::duration fifo_depth_duration() const = 0;
  virtual zx_koid_t stream_channel_koid() const = 0;
  virtual const HwGainState& hw_gain_state() const = 0;

  // The following properties are only safe to access after the driver is beyond the
  // MissingDriverInfo state.  After that state, these members must be treated as immutable, and the
  // driver class may no longer change them.
  virtual const audio_stream_unique_id_t& persistent_unique_id() const = 0;
  virtual const std::string& manufacturer_name() const = 0;
  virtual const std::string& product_name() const = 0;

  virtual zx_status_t GetDriverInfo() = 0;
  virtual zx_status_t Configure(const Format& format, zx::duration min_ring_buffer_duration) = 0;
  virtual zx_status_t Start() = 0;
  virtual zx_status_t Stop() = 0;
  virtual zx_status_t SetPlugDetectEnabled(bool enabled) = 0;
  virtual zx_status_t SetGain(const AudioDeviceSettings::GainState& gain_state,
                              audio_set_gain_flags_t set_flags) = 0;
  virtual zx_status_t SelectBestFormat(uint32_t* frames_per_second_inout, uint32_t* channels_inout,
                                       fuchsia::media::AudioSampleFormat* sample_format_inout) = 0;
  virtual const std::shared_ptr<ReadableRingBuffer>& readable_ring_buffer() const
      FXL_NO_THREAD_SAFETY_ANALYSIS = 0;
  virtual const std::shared_ptr<WritableRingBuffer>& writable_ring_buffer() const
      FXL_NO_THREAD_SAFETY_ANALYSIS = 0;
  virtual const TimelineFunction& ref_time_to_frac_presentation_frame() const = 0;
  virtual const TimelineFunction& ref_time_to_safe_read_or_write_frame() const = 0;

  virtual AudioClock& reference_clock() = 0;
};

// TODO(41922): Remove AudioDriverV1 once the transition to V2 is completed.
class AudioDriverV1 : public AudioDriver {
 public:
  AudioDriverV1(AudioDevice* owner);

  using DriverTimeoutHandler = fit::function<void(zx::duration)>;
  AudioDriverV1(AudioDevice* owner, DriverTimeoutHandler timeout_handler);

  virtual ~AudioDriverV1() = default;

  zx_status_t Init(zx::channel stream_channel) override;
  void Cleanup() override;
  std::optional<Format> GetFormat() const override;

  bool plugged() const override {
    std::lock_guard<std::mutex> lock(plugged_lock_);
    return plugged_;
  }

  zx::time plug_time() const override {
    std::lock_guard<std::mutex> lock(plugged_lock_);
    return plug_time_;
  }

  State state() const override { return state_; }
  zx::time ref_start_time() const override { return ref_start_time_; }
  zx::duration external_delay() const override { return external_delay_; }
  uint32_t fifo_depth_frames() const override { return fifo_depth_frames_; }
  zx::duration fifo_depth_duration() const override { return fifo_depth_duration_; }
  zx_koid_t stream_channel_koid() const override { return stream_channel_koid_; }
  const HwGainState& hw_gain_state() const override { return hw_gain_state_; }

  const TimelineFunction& ref_time_to_frac_presentation_frame() const override {
    return ref_time_to_frac_presentation_frame__;
  }
  const TimelineFunction& ref_time_to_safe_read_or_write_frame() const override {
    return ref_time_to_safe_read_or_write_frame_;
  }

  const audio_stream_unique_id_t& persistent_unique_id() const override {
    return persistent_unique_id_;
  }
  const std::string& manufacturer_name() const override { return manufacturer_name_; }
  const std::string& product_name() const override { return product_name_; }

  zx_status_t GetDriverInfo() override;
  zx_status_t Configure(const Format& format, zx::duration min_ring_buffer_duration) override;
  zx_status_t Start() override;
  zx_status_t Stop() override;
  zx_status_t SetPlugDetectEnabled(bool enabled) override;
  zx_status_t SetGain(const AudioDeviceSettings::GainState& gain_state,
                      audio_set_gain_flags_t set_flags) override;
  zx_status_t SelectBestFormat(uint32_t* frames_per_second_inout, uint32_t* channels_inout,
                               fuchsia::media::AudioSampleFormat* sample_format_inout) override;

  AudioClock& reference_clock() override { return audio_clock_; }

 private:
  friend class AudioDevice;
  friend class AudioInput;

  static constexpr uint32_t kDriverInfoHasUniqueId = (1u << 0);
  static constexpr uint32_t kDriverInfoHasMfrStr = (1u << 1);
  static constexpr uint32_t kDriverInfoHasProdStr = (1u << 2);
  static constexpr uint32_t kDriverInfoHasGainState = (1u << 3);
  static constexpr uint32_t kDriverInfoHasFormats = (1u << 4);
  static constexpr uint32_t kDriverInfoHasClockDomain = (1u << 5);
  static constexpr uint32_t kDriverInfoHasAll = kDriverInfoHasUniqueId | kDriverInfoHasMfrStr |
                                                kDriverInfoHasProdStr | kDriverInfoHasGainState |
                                                kDriverInfoHasFormats | kDriverInfoHasClockDomain;

  // Counter of received position notifications since START.
  uint32_t position_notification_count_ = 0;

  // Dispatchers for messages received over stream and ring buffer channels.
  zx_status_t ReadMessage(const zx::channel& channel, void* buf, uint32_t buf_size,
                          uint32_t* bytes_read_out, zx::handle* handle_out)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());
  zx_status_t ProcessStreamChannelMessage()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());
  zx_status_t ProcessRingBufferChannelMessage()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());

  // Stream channel message handlers.
  zx_status_t ProcessGetStringResponse(audio_stream_cmd_get_string_resp_t& resp)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());
  zx_status_t ProcessGetGainResponse(audio_stream_cmd_get_gain_resp_t& resp)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());
  zx_status_t ProcessGetFormatsResponse(const audio_stream_cmd_get_formats_resp_t& resp)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());
  zx_status_t ProcessSetFormatResponse(const audio_stream_cmd_set_format_resp_t& resp,
                                       zx::channel rb_channel)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());
  zx_status_t ProcessGetClockDomainResponse(audio_stream_cmd_get_clock_domain_resp_t& resp)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());

  // Ring buffer message handlers.
  zx_status_t ProcessGetFifoDepthResponse(const audio_rb_cmd_get_fifo_depth_resp_t& resp)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());
  zx_status_t ProcessGetBufferResponse(const audio_rb_cmd_get_buffer_resp_t& resp, zx::vmo rb_vmo)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());
  zx_status_t ProcessStartResponse(const audio_rb_cmd_start_resp_t& resp)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());
  zx_status_t ProcessStopResponse(const audio_rb_cmd_stop_resp_t& resp)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());
  zx_status_t ProcessPositionNotify(const audio_rb_position_notify_t& notify)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());

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

  // Accessors for the ring buffer pointer and the current output clock transformation.
  //
  // Note: Only AudioDriverV1 writes to these, and only when in our owner's mixing execution
  // domain.  It is safe for our owner to read these objects, but only when operating in the mixing
  // domain.  Unfortunately, it is not practical to use the static thread safety annotation to prove
  // that we are accessing these variable from the mixing domain.  Instead, we...
  //
  // 1) Make these methods private.
  // 2) Make the AudioDevice class (our owner) a friend.
  // 3) Expose protected accessors in AudioDevice which demand that we execute in the mix domain.
  //
  // This should be a strong enough guarantee to warrant disabling the thread safety analysis here.
  const std::shared_ptr<ReadableRingBuffer>& readable_ring_buffer() const override
      FXL_NO_THREAD_SAFETY_ANALYSIS {
    return readable_ring_buffer_;
  };
  const std::shared_ptr<WritableRingBuffer>& writable_ring_buffer() const override
      FXL_NO_THREAD_SAFETY_ANALYSIS {
    return writable_ring_buffer_;
  };

  void StreamChannelSignalled(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                              zx_status_t status, const zx_packet_signal_t* signal)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());
  void RingBufferChannelSignalled(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                  zx_status_t status, const zx_packet_signal_t* signal)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());

  void DriverCommandTimedOut() FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());

  AudioDevice* const owner_;
  DriverTimeoutHandler timeout_handler_;

  State state_ = State::Uninitialized;
  zx::channel stream_channel_;
  zx::channel ring_buffer_channel_;

  async::Wait stream_channel_wait_ FXL_GUARDED_BY(owner_->mix_domain().token());
  async::Wait ring_buffer_channel_wait_ FXL_GUARDED_BY(owner_->mix_domain().token());
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
  zx::time configuration_deadline_ = zx::time::infinite();

  // A stashed copy of current format, queryable by destinations (outputs or AudioCapturers) when
  // determining which mixer to use.
  mutable std::mutex configured_format_lock_;
  std::optional<Format> configured_format_ FXL_GUARDED_BY(configured_format_lock_);

  // Ring buffer state. Details are lock-protected and changes tracked with generation counter,
  // allowing AudioCapturer clients to snapshot ring-buffer state during mix/resample operations.
  mutable std::mutex ring_buffer_state_lock_;
  std::shared_ptr<ReadableRingBuffer> readable_ring_buffer_ FXL_GUARDED_BY(ring_buffer_state_lock_);
  std::shared_ptr<WritableRingBuffer> writable_ring_buffer_ FXL_GUARDED_BY(ring_buffer_state_lock_);

  // The timeline function which maps from either the capture time (Input) or
  // presentation time (Output) at the speaker/microphone on the audio device's
  // reference clock, to the fractional frame position in the stream.
  //
  // IOW - given a frame number in the stream, the inverse of this function can
  // be used to map to the time (on the device's reference clock) that the frame
  // either was captured, or will be presented.
  fbl::RefPtr<VersionedTimelineFunction> versioned_ref_time_to_frac_presentation_frame__;

  // Useful timeline functions which are computed after streaming starts.  See
  // the comments for the accessors in audio_device.h for detailed descriptions.
  TimelineFunction ref_time_to_frac_presentation_frame__
      FXL_GUARDED_BY(owner_->mix_domain().token());
  TimelineFunction ref_time_to_safe_read_or_write_frame_
      FXL_GUARDED_BY(owner_->mix_domain().token());

  // Plug detection state.
  bool pd_enabled_ = false;
  zx::time pd_enable_deadline_ = zx::time::infinite();

  mutable std::mutex plugged_lock_;
  bool plugged_ FXL_GUARDED_BY(plugged_lock_) = false;
  zx::time plug_time_ FXL_GUARDED_BY(plugged_lock_);

  zx::time driver_last_timeout_ = zx::time::infinite();

  // fuchsia::hardware::audio::CLOCK_DOMAIN_MONOTONIC is not defined for AudioDriverV1 types.
  uint32_t clock_domain_ = 0;
  AudioClock audio_clock_;
};

class AudioDriverV2 : public AudioDriver {
 public:
  AudioDriverV2(AudioDevice* owner);

  using DriverTimeoutHandler = fit::function<void(zx::duration)>;
  AudioDriverV2(AudioDevice* owner, DriverTimeoutHandler timeout_handler);

  virtual ~AudioDriverV2() = default;

  zx_status_t Init(zx::channel stream_channel) override;
  void Cleanup() override;
  std::optional<Format> GetFormat() const override;

  bool plugged() const override {
    std::lock_guard<std::mutex> lock(plugged_lock_);
    return plugged_;
  }

  zx::time plug_time() const override {
    std::lock_guard<std::mutex> lock(plugged_lock_);
    return plug_time_;
  }

  State state() const override { return state_; }
  zx::time ref_start_time() const override { return ref_start_time_; }
  zx::duration external_delay() const override { return external_delay_; }
  uint32_t fifo_depth_frames() const override { return fifo_depth_frames_; }
  zx::duration fifo_depth_duration() const override { return fifo_depth_duration_; }
  zx_koid_t stream_channel_koid() const override { return stream_channel_koid_; }
  const HwGainState& hw_gain_state() const override { return hw_gain_state_; }

  const TimelineFunction& ref_time_to_frac_presentation_frame() const override {
    return ref_time_to_frac_presentation_frame__;
  }
  const TimelineFunction& ref_time_to_safe_read_or_write_frame() const override {
    return ref_time_to_safe_read_or_write_frame_;
  }

  const audio_stream_unique_id_t& persistent_unique_id() const override {
    return persistent_unique_id_;
  }
  const std::string& manufacturer_name() const override { return manufacturer_name_; }
  const std::string& product_name() const override { return product_name_; }

  zx_status_t GetDriverInfo() override;
  zx_status_t Configure(const Format& format, zx::duration min_ring_buffer_duration) override;
  zx_status_t Start() override;
  zx_status_t Stop() override;
  zx_status_t SetPlugDetectEnabled(bool enabled) override;
  zx_status_t SetGain(const AudioDeviceSettings::GainState& gain_state,
                      audio_set_gain_flags_t set_flags) override;
  zx_status_t SelectBestFormat(uint32_t* frames_per_second_inout, uint32_t* channels_inout,
                               fuchsia::media::AudioSampleFormat* sample_format_inout) override;

  AudioClock& reference_clock() override { return audio_clock_; }

 private:
  static constexpr uint32_t kDriverInfoHasUniqueId = (1u << 0);
  static constexpr uint32_t kDriverInfoHasMfrStr = (1u << 1);
  static constexpr uint32_t kDriverInfoHasProdStr = (1u << 2);
  static constexpr uint32_t kDriverInfoHasGainState = (1u << 3);
  static constexpr uint32_t kDriverInfoHasFormats = (1u << 4);
  static constexpr uint32_t kDriverInfoHasClockDomain = (1u << 5);
  static constexpr uint32_t kDriverInfoHasAll = kDriverInfoHasUniqueId | kDriverInfoHasMfrStr |
                                                kDriverInfoHasProdStr | kDriverInfoHasGainState |
                                                kDriverInfoHasFormats | kDriverInfoHasClockDomain;

  // Counter of received position notifications since START.
  uint32_t position_notification_count_ = 0;

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

  // Accessors for the ring buffer pointer and the current output clock transformation.
  //
  // Note: Only AudioDriverV2 writes to these, and only when in our owner's mixing execution
  // domain.  It is safe for our owner to read these objects, but only when operating in the mixing
  // domain.  Unfortunately, it is not practical to use the static thread safety annotation to prove
  // that we are accessing these variable from the mixing domain.  Instead, we...
  //
  // 1) Make these methods private.
  // 2) Make the AudioDevice class (our owner) a friend.
  // 3) Expose protected accessors in AudioDevice which demand that we execute in the mix domain.
  //
  // This should be a strong enough guarantee to warrant disabling the thread safety analysis here.
  const std::shared_ptr<ReadableRingBuffer>& readable_ring_buffer() const override
      FXL_NO_THREAD_SAFETY_ANALYSIS {
    return readable_ring_buffer_;
  };
  const std::shared_ptr<WritableRingBuffer>& writable_ring_buffer() const override
      FXL_NO_THREAD_SAFETY_ANALYSIS {
    return writable_ring_buffer_;
  };

  void DriverCommandTimedOut() FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());
  void RestartWatchPlugState() FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());
  void RestartWatchClockRecovery() FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain().token());

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
  zx::time configuration_deadline_ = zx::time::infinite();

  // A stashed copy of current format, queryable by destinations (outputs or AudioCapturers) when
  // determining which mixer to use.
  mutable std::mutex configured_format_lock_;
  std::optional<Format> configured_format_ FXL_GUARDED_BY(configured_format_lock_);

  // Ring buffer state. Details are lock-protected and changes tracked with generation counter,
  // allowing AudioCapturer clients to snapshot ring-buffer state during mix/resample operations.
  mutable std::mutex ring_buffer_state_lock_;
  std::shared_ptr<ReadableRingBuffer> readable_ring_buffer_ FXL_GUARDED_BY(ring_buffer_state_lock_);
  std::shared_ptr<WritableRingBuffer> writable_ring_buffer_ FXL_GUARDED_BY(ring_buffer_state_lock_);

  // The timeline function which maps from either the capture time (Input) or
  // presentation time (Output) at the speaker/microphone on the audio device's
  // reference clock, to the fractional frame position in the stream.
  //
  // IOW - given a frame number in the stream, the inverse of this function can
  // be used to map to the time (on the device's reference clock) that the frame
  // either was captured, or will be presented.
  fbl::RefPtr<VersionedTimelineFunction> versioned_ref_time_to_frac_presentation_frame__;

  // Useful timeline functions which are computed after streaming starts.  See
  // the comments for the accessors in audio_device.h for detailed descriptions.
  TimelineFunction ref_time_to_frac_presentation_frame__
      FXL_GUARDED_BY(owner_->mix_domain().token());
  TimelineFunction ref_time_to_safe_read_or_write_frame_
      FXL_GUARDED_BY(owner_->mix_domain().token());

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

  uint32_t clock_domain_ = fuchsia::hardware::audio::CLOCK_DOMAIN_MONOTONIC;
  AudioClock audio_clock_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DRIVER_H_
