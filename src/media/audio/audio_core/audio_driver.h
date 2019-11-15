// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DRIVER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DRIVER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>
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

  struct RingBufferSnapshot {
    fbl::RefPtr<RingBuffer> ring_buffer;
    TimelineFunction clock_mono_to_ring_pos_bytes;
    uint32_t position_to_end_fence_frames;
    uint32_t end_fence_to_start_fence_frames;
    uint32_t gen_id;
  };

  AudioDriver(AudioDevice* owner);

  using DriverTimeoutHandler = fit::function<void(zx::duration)>;
  AudioDriver(AudioDevice* owner, DriverTimeoutHandler timeout_handler);

  virtual ~AudioDriver() = default;

  zx_status_t Init(zx::channel stream_channel);
  void Cleanup();
  void SnapshotRingBuffer(RingBufferSnapshot* snapshot) const;
  fuchsia::media::AudioStreamTypePtr GetSourceFormat() const;

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
  const std::vector<audio_stream_format_range_t>& format_ranges() const { return format_ranges_; }

  State state() const { return state_; }
  uint32_t frames_per_sec() const { return frames_per_sec_; }
  zx::duration external_delay() const { return external_delay_; }
  uint16_t channel_count() const { return channel_count_; }
  audio_sample_format_t sample_format() const { return sample_format_; }
  uint32_t bytes_per_frame() const { return bytes_per_frame_; }
  uint32_t fifo_depth_bytes() const { return fifo_depth_bytes_; }
  uint32_t fifo_depth_frames() const { return fifo_depth_frames_; }
  zx::duration fifo_depth_duration() const { return fifo_depth_duration_; }
  zx_koid_t stream_channel_koid() const { return stream_channel_koid_; }
  const HwGainState& hw_gain_state() const { return hw_gain_state_; }

  // The following properties are only safe to access after the driver is beyond the
  // MissingDriverInfo state.  After that state, these members must be treated as immutable, and the
  // driver class may no longer change them.
  const audio_stream_unique_id_t& persistent_unique_id() const { return persistent_unique_id_; }
  const std::string& manufacturer_name() const { return manufacturer_name_; }
  const std::string& product_name() const { return product_name_; }

  void SetEndFenceToStartFenceFrames(uint32_t dist) {
    std::lock_guard<std::mutex> lock(ring_buffer_state_lock_);
    end_fence_to_start_fence_frames_ = dist;
  }

  zx_status_t GetDriverInfo();
  zx_status_t Configure(uint32_t frames_per_second, uint32_t channels,
                        fuchsia::media::AudioSampleFormat fmt,
                        zx::duration min_ring_buffer_duration);
  zx_status_t Start();
  zx_status_t Stop();
  zx_status_t SetPlugDetectEnabled(bool enabled);
  zx_status_t SendSetGain(const AudioDeviceSettings::GainState& gain_state,
                          audio_set_gain_flags_t set_flags);

 private:
  friend class AudioDevice;
  friend class AudioInput;

  static constexpr uint32_t kDriverInfoHasUniqueId = (1u << 0);
  static constexpr uint32_t kDriverInfoHasMfrStr = (1u << 1);
  static constexpr uint32_t kDriverInfoHasProdStr = (1u << 2);
  static constexpr uint32_t kDriverInfoHasGainState = (1u << 3);
  static constexpr uint32_t kDriverInfoHasFormats = (1u << 4);
  static constexpr uint32_t kDriverInfoHasAll = kDriverInfoHasUniqueId | kDriverInfoHasMfrStr |
                                                kDriverInfoHasProdStr | kDriverInfoHasGainState |
                                                kDriverInfoHasFormats;

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
  // Note: Only the AudioDriver writes to these, and only when in our owner's mixing execution
  // domain.  It is safe for our owner to read these objects, but only when operating in the mixing
  // domain.  Unfortunately, it is not practical to use the static thread safety annotation to prove
  // that we are accessing these variable from the mixing domain.  Instead, we...
  //
  // 1) Make these methods private.
  // 2) Make the AudioDevice class (our owner) a friend.
  // 3) Expose protected accessors in AudioDevice which demand that we execute in the mix domain.
  //
  // This should be a strong enough guarantee to warrant disabling the thread safety analysis here.
  const fbl::RefPtr<RingBuffer>& ring_buffer() const FXL_NO_THREAD_SAFETY_ANALYSIS {
    return ring_buffer_;
  };

  const TimelineFunction& clock_mono_to_ring_pos_bytes() const FXL_NO_THREAD_SAFETY_ANALYSIS {
    return clock_mono_to_ring_pos_bytes_;
  }
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
  uint32_t frames_per_sec_;
  zx::duration external_delay_;
  uint16_t channel_count_;
  audio_sample_format_t sample_format_;
  uint32_t bytes_per_frame_;
  zx::duration min_ring_buffer_duration_;
  uint32_t fifo_depth_bytes_;
  uint32_t fifo_depth_frames_;
  zx::duration fifo_depth_duration_;
  zx::time configuration_deadline_ = zx::time::infinite();

  // A stashed copy of current format, queryable by destinations (outputs or AudioCapturers) when
  // determining which mixer to use.
  mutable std::mutex configured_format_lock_;
  fuchsia::media::AudioStreamTypePtr configured_format_ FXL_GUARDED_BY(configured_format_lock_);

  // Ring buffer state. Details are lock-protected and changes tracked with generation counter,
  // allowing AudioCapturer clients to snapshot ring-buffer state during mix/resample operations.
  mutable std::mutex ring_buffer_state_lock_;
  fbl::RefPtr<RingBuffer> ring_buffer_ FXL_GUARDED_BY(ring_buffer_state_lock_);
  TimelineFunction clock_mono_to_ring_pos_bytes_ FXL_GUARDED_BY(ring_buffer_state_lock_);
  uint32_t end_fence_to_start_fence_frames_ FXL_GUARDED_BY(ring_buffer_state_lock_) = 0;
  GenerationId ring_buffer_state_gen_ FXL_GUARDED_BY(ring_buffer_state_lock_);

  // Plug detection state.
  bool pd_enabled_ = false;
  zx::time pd_enable_deadline_ = zx::time::infinite();

  mutable std::mutex plugged_lock_;
  bool plugged_ FXL_GUARDED_BY(plugged_lock_) = false;
  zx::time plug_time_ FXL_GUARDED_BY(plugged_lock_);

  zx::time driver_last_timeout_ = zx::time::infinite();
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_DRIVER_H_
