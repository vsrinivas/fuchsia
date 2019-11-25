// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CAPTURER_IMPL_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CAPTURER_IMPL_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/media/cpp/timeline_function.h>
#include <lib/media/cpp/timeline_rate.h>

#include <atomic>
#include <memory>
#include <mutex>

#include <fbl/intrusive_double_list.h>
#include <fbl/slab_allocator.h>

#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/audio_core/audio_link.h"
#include "src/media/audio/audio_core/audio_object.h"
#include "src/media/audio/audio_core/mixer/mixer.h"
#include "src/media/audio/audio_core/mixer/output_producer.h"
#include "src/media/audio/audio_core/route_graph.h"
#include "src/media/audio/audio_core/stream_volume_manager.h"
#include "src/media/audio/audio_core/threading_model.h"
#include "src/media/audio/audio_core/utils.h"

namespace media::audio {

class AudioAdmin;
class AudioCoreImpl;
class AudioDeviceManager;

class AudioCapturerImpl : public AudioObject,
                          public fuchsia::media::AudioCapturer,
                          public fuchsia::media::audio::GainControl,
                          public StreamVolume,
                          public fbl::Recyclable<AudioCapturerImpl> {
 public:
  static fbl::RefPtr<AudioCapturerImpl> Create(
      bool loopback, fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request,
      AudioCoreImpl* owner);
  static fbl::RefPtr<AudioCapturerImpl> Create(
      bool loopback, fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request,
      ThreadingModel* threading_model, RouteGraph* route_graph, AudioAdmin* admin,
      StreamVolumeManager* volume_manager);

  ~AudioCapturerImpl() override;

  bool loopback() const { return loopback_; }
  void SetInitialFormat(fuchsia::media::AudioStreamType format)
      FXL_LOCKS_EXCLUDED(mix_domain_->token());

  void SetUsage(fuchsia::media::AudioCaptureUsage usage) override;
  fuchsia::media::AudioCaptureUsage GetUsage() { return usage_; };

  void OverflowOccurred(int64_t source_start, int64_t mix_point, zx::duration overflow_duration);
  void PartialOverflowOccurred(int64_t source_offset, int64_t mix_offset);

 protected:
  friend class fbl::RefPtr<AudioCapturerImpl>;
  zx_status_t InitializeSourceLink(const fbl::RefPtr<AudioLink>& link) override;

 private:
  // Notes about the AudioCapturerImpl state machine.
  // TODO(mpuryear): Update this comment block.
  //
  // :: WaitingForVmo ::
  // AudioCapturers start in this mode. They should have a default capture mode
  // set, and will accept a mode change up until the point where they have a
  // shared payload VMO assigned to them. At this point they transition into the
  // OperatingSync state. Only the main service thread may transition out of
  // this state.
  //
  // :: OperatingSync ::
  // After a mode has been assigned and a shared payload VMO has provided, the
  // AudioCapturer is now operating in synchronous mode. Clients may provided
  // buffers to be filled using the CaptureAt method and may cancel these
  // buffers using the Flush method. They may also transition to asynchronous
  // mode by calling StartAsyncCapture, but only when there are no pending
  // buffers in flight. Only the main service thread may transition out of
  // this state.
  //
  // :: OperatingAsync ::
  // AudioCapturers enter OperatingAsync after a successful call to
  // StartAsyncCapture. Threads from the mix_domain allocate and fill pending
  // payload buffers, then signal the main service thread in order to send them
  // back to the client over the AudioCapturerClient interface provided when
  // starting. CaptureAt and Flush are illegal operations while in this state.
  // clients may begin the process of returning to synchronous capture mode by
  // calling StopAsyncCapture. Only the main service thread may transition out
  // of this state.
  //
  // :: AsyncStopping ::
  // AudioCapturers enter AsyncStopping after a successful call to
  // StopAsyncCapture. A thread from the mix_domain will handle the details of
  // stopping, including transferring all partially filled pending buffers to
  // the finished queue. Aside from setting the gain, all operations are illegal
  // while the AudioCapturer is in the process of stopping. Once the mix domain
  // thread has finished cleaning up, it will transition to the
  // AsyncStoppingCallbackPending state and signal the main service thread in
  // order to complete the process. Only a mix domain thread may transition out
  // of this state.
  //
  // :: AsyncStoppingCallbackPending ::
  // AudioCapturers enter AsyncStoppingCallbackPending after a mix domain thread
  // has finished the process of shutting down the capture process and is ready
  // to signal to the client that the AudioCapturer is now in synchronous
  // capture mode again. The main service thread will send all partially and
  // completely filled buffers to the user, ensuring that there is at least one
  // buffer sent indicating end-of-stream, even if that buffer needs to be of
  // zero length. Finally, the main service thread will signal that the stopping
  // process is finished using the client supplied callback (if any), and
  // finally transition back to the OperatingSync state.
  enum class State {
    WaitingForVmo,
    OperatingSync,
    OperatingAsync,
    AsyncStopping,
    AsyncStoppingCallbackPending,
    Shutdown,
  };

  static bool StateIsRoutable(AudioCapturerImpl::State state) {
    return state != AudioCapturerImpl::State::WaitingForVmo &&
           state != AudioCapturerImpl::State::Shutdown;
  }

  struct PendingCaptureBuffer;

  using PcbAllocatorTraits =
      ::fbl::StaticSlabAllocatorTraits<std::unique_ptr<PendingCaptureBuffer>>;
  using PcbAllocator = ::fbl::SlabAllocator<PcbAllocatorTraits>;
  using PcbList = ::fbl::DoublyLinkedList<std::unique_ptr<PendingCaptureBuffer>>;

  struct PendingCaptureBuffer
      : public fbl::SlabAllocated<PcbAllocatorTraits>,
        public fbl::DoublyLinkedListable<std::unique_ptr<PendingCaptureBuffer>> {
    PendingCaptureBuffer(uint32_t of, uint32_t nf, CaptureAtCallback c)
        : offset_frames(of), num_frames(nf), cbk(std::move(c)) {}

    static AtomicGenerationId sequence_generator;

    const uint32_t offset_frames;
    const uint32_t num_frames;
    const CaptureAtCallback cbk;

    int64_t capture_timestamp = fuchsia::media::NO_TIMESTAMP;
    uint32_t flags = 0;
    uint32_t filled_frames = 0;
    const uint32_t sequence_number = sequence_generator.Next();
  };

  friend PcbAllocator;

  std::vector<fuchsia::media::AudioCaptureUsage> allowed_usages_;
  fuchsia::media::AudioCaptureUsage usage_;

  AudioCapturerImpl(bool loopback,
                    fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request,
                    ThreadingModel* threading_model, RouteGraph* route_graph, AudioAdmin* admin,
                    StreamVolumeManager* volume_manager);

  // AudioCapturer FIDL implementation
  void GetStreamType(GetStreamTypeCallback cbk) final;
  void SetPcmStreamType(fuchsia::media::AudioStreamType stream_type) final;
  void AddPayloadBuffer(uint32_t id, zx::vmo payload_buf_vmo) final;
  void RemovePayloadBuffer(uint32_t id) final;
  void CaptureAt(uint32_t payload_buffer_id, uint32_t offset_frames, uint32_t num_frames,
                 CaptureAtCallback cbk) final;
  void ReleasePacket(fuchsia::media::StreamPacket packet) final;
  void DiscardAllPackets(DiscardAllPacketsCallback cbk) final;
  void DiscardAllPacketsNoReply() final;
  void StartAsyncCapture(uint32_t frames_per_packet) final;
  void StopAsyncCapture(StopAsyncCaptureCallback cbk) final;
  void StopAsyncCaptureNoReply() final;
  void BindGainControl(fidl::InterfaceRequest<fuchsia::media::audio::GainControl> request) final;

  // |fuchsia::media::audio::GainControl|
  void SetGain(float gain_db) final;
  void SetGainWithRamp(float gain_db, int64_t duration_ns,
                       fuchsia::media::audio::RampType ramp_type) final {
    FX_NOTIMPLEMENTED();
  }
  void SetMute(bool mute) final;
  void NotifyGainMuteChanged();

  void ReportStart();
  void ReportStop();

  // AudioObject overrides.
  void OnLinkAdded() override;

  // StreamVolume interface.
  bool GetStreamMute() const final;
  fuchsia::media::Usage GetStreamUsage() const final;
  void RealizeVolume(VolumeCommand volume_command) final;

  // Methods used by capture/mixer thread(s). Must be called from mix_domain.
  zx_status_t Process() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  bool MixToIntermediate(uint32_t mix_frames) FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  void UpdateTransformation(Mixer::Bookkeeping* bk, const AudioDriver::RingBufferSnapshot& rb_snap)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  void DoStopAsyncCapture() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  bool QueueNextAsyncPendingBuffer() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token())
      FXL_LOCKS_EXCLUDED(pending_lock_);
  void ShutdownFromMixDomain() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  // Thunk to send finished buffers back to the user, and to finish an async
  // mode stop operation.
  void FinishAsyncStopThunk() FXL_LOCKS_EXCLUDED(mix_domain_->token());
  void FinishBuffersThunk() FXL_LOCKS_EXCLUDED(mix_domain_->token());

  // Helper function used to return a set of pending capture buffers to a user.
  void FinishBuffers(const PcbList& finished_buffers) FXL_LOCKS_EXCLUDED(mix_domain_->token());

  // Mixer helper.
  void UpdateFormat(fuchsia::media::AudioSampleFormat sample_format, uint32_t channels,
                    uint32_t frames_per_second) FXL_LOCKS_EXCLUDED(mix_domain_->token());

  // Select a mixer for the link supplied and return true, or return false if one cannot be found.
  zx_status_t ChooseMixer(const fbl::RefPtr<AudioLink>& link);

  fit::promise<> Cleanup() FXL_LOCKS_EXCLUDED(mix_domain_->token());
  void CleanupFromMixThread() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  void MixTimerThunk() {
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, mix_domain_);
    Process();
  }

  // Removes the capturer from its owner, the route graph, triggering shutdown and drop.
  void BeginShutdown();

  // TODO(39624): Remove
  friend class fbl::Recyclable<AudioCapturerImpl>;
  void fbl_recycle() { RecycleObject(this); }

  virtual void RecycleObject(AudioObject* object) final;

  void RecomputeMinFenceTime();

  void Shutdown(std::unique_ptr<AudioCapturerImpl> self)
      FXL_LOCKS_EXCLUDED(threading_model_.FidlDomain().token());

  fidl::Binding<fuchsia::media::AudioCapturer> binding_;
  fidl::BindingSet<fuchsia::media::audio::GainControl> gain_control_bindings_;
  ThreadingModel& threading_model_;
  ThreadingModel::OwnedDomainPtr mix_domain_;
  AudioAdmin& admin_;
  StreamVolumeManager& volume_manager_;
  RouteGraph& route_graph_;
  std::atomic<State> state_;
  const bool loopback_;
  zx::duration min_fence_time_;

  // Capture format and gain state.
  fuchsia::media::AudioStreamType format_;
  uint32_t bytes_per_frame_;
  TimelineRate dest_frames_to_clock_mono_rate_;
  uint32_t max_frames_per_capture_;
  std::atomic<float> stream_gain_db_;
  bool mute_;

  // Shared buffer state
  zx::vmo payload_buf_vmo_;
  void* payload_buf_virt_ = nullptr;
  uint64_t payload_buf_size_ = 0;
  uint32_t payload_buf_frames_ = 0;

  WakeupEvent mix_wakeup_;
  async::TaskClosureMethod<AudioCapturerImpl, &AudioCapturerImpl::MixTimerThunk> mix_timer_
      FXL_GUARDED_BY(mix_domain_->token()){this};

  // Queues of capture buffers from the client: waiting to be filled, or waiting to be returned.
  std::mutex pending_lock_;
  PcbList pending_capture_buffers_ FXL_GUARDED_BY(pending_lock_);
  PcbList finished_capture_buffers_ FXL_GUARDED_BY(pending_lock_);

  // Intermediate mixing buffer and output producer
  std::unique_ptr<OutputProducer> output_producer_;
  std::unique_ptr<float[]> mix_buf_;

  // Vector used to hold references to our source links while we are mixing
  // (instead of holding the lock which prevents source_links_ mutation for the
  // entire mix job)
  std::vector<fbl::RefPtr<AudioLink>> source_link_refs_ FXL_GUARDED_BY(mix_domain_->token());

  // Capture bookkeeping
  bool async_mode_ = false;
  TimelineFunction dest_frames_to_clock_mono_ FXL_GUARDED_BY(mix_domain_->token());
  GenerationId dest_frames_to_clock_mono_gen_ FXL_GUARDED_BY(mix_domain_->token());
  int64_t frame_count_ FXL_GUARDED_BY(mix_domain_->token()) = 0;

  uint32_t async_frames_per_packet_;
  uint32_t async_next_frame_offset_ FXL_GUARDED_BY(mix_domain_->token()) = 0;
  StopAsyncCaptureCallback pending_async_stop_cbk_;

  // for glitch-debugging purposes
  std::atomic<uint16_t> overflow_count_;
  std::atomic<uint16_t> partial_overflow_count_;
};

}  // namespace media::audio

FWD_DECL_STATIC_SLAB_ALLOCATOR(media::audio::AudioCapturerImpl::PcbAllocatorTraits);

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_CAPTURER_IMPL_H_
