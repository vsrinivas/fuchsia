// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_ptr.h>
#include <fbl/vmo_mapper.h>

#include "garnet/bin/media/audio_server/audio_object.h"
#include "garnet/bin/media/audio_server/audio_renderer_impl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/media/fidl/audio_renderer.fidl.h"

namespace media {
namespace audio {

class AudioServerImpl;

class AudioRenderer2Impl : public AudioRendererImpl,
                           public AudioRenderer2 {
 public:
  static fbl::RefPtr<AudioRenderer2Impl> Create(
      f1dl::InterfaceRequest<AudioRenderer2> audio_renderer_request,
      AudioServerImpl* owner);

  // AudioRendererImpl implementation
  // TODO(johngro) : Collapse AudioRendererImpl into AudioRenderer2Impl when
  // AudioRenderer1Impl has been fully depricated and removed.
  void Shutdown() override;
  void OnRenderRange(int64_t presentation_time, uint32_t duration) override { };
  void SnapshotCurrentTimelineFunction(
      int64_t reference_time,
      TimelineFunction* out,
      uint32_t* generation) override;


  // AudioRenderer2 Interface
  void SetPcmFormat(AudioPcmFormatPtr format) final;
  void SetPayloadBuffer(zx::vmo payload_buffer) final;
  void SetPtsUnits(uint32_t tick_per_second_numerator,
                   uint32_t tick_per_second_denominator) final;
  void SetPtsContinuityThreshold(float threshold_seconds) final;
  void SetReferenceClock(zx::handle ref_clock) final;
  void SendPacket(AudioPacketPtr packet,
                  const SendPacketCallback& callback) final;
  void SendPacketNoReply(AudioPacketPtr packet) final;
  void Flush(const FlushCallback& callback) final;
  void FlushNoReply() final;
  void Play(int64_t reference_time,
            int64_t media_time,
            const PlayCallback& callback) final;
  void PlayNoReply(int64_t reference_time, int64_t media_time) final;
  void Pause(const PauseCallback& callback) final;
  void PauseNoReply() final;
  void SetGainMute(float gain,
                   bool mute,
                   uint32_t flags,
                   const SetGainMuteCallback& callback) final;
  void SetGainMuteNoReply(float gain, bool mute, uint32_t flags) final;
  void DuplicateGainControlInterface(
      f1dl::InterfaceRequest<AudioRendererGainControl> request) final;
  void EnableMinLeadTimeEvents(
      f1dl::InterfaceHandle<AudioRendererMinLeadTimeChangedEvent> evt) final;
  void GetMinLeadTime(const GetMinLeadTimeCallback& callback) final;

 private:
  class GainControlBinding : public AudioRendererGainControl {
   public:
    static fbl::unique_ptr<GainControlBinding> Create(
        AudioRenderer2Impl* owner) {
      return fbl::unique_ptr<GainControlBinding>(new GainControlBinding(owner));
    }

    bool gain_events_enabled() const { return gain_events_enabled_; }

    // AudioRendererGainControl
    void SetGainMute(float gain,
                     bool mute,
                     uint32_t flags,
                     const SetGainMuteCallback& callback) override;
    void SetGainMuteNoReply(float gain, bool mute, uint32_t flags) override;

   private:
    friend class fbl::unique_ptr<GainControlBinding>;

    GainControlBinding(AudioRenderer2Impl* owner) : owner_(owner) {}
    ~GainControlBinding() override {}

    AudioRenderer2Impl* owner_;
    bool gain_events_enabled_ = false;
  };

  friend class fbl::RefPtr<AudioRenderer2Impl>;
  friend class GainControlBinding;

  AudioRenderer2Impl(
      f1dl::InterfaceRequest<AudioRenderer2> audio_renderer_request,
      AudioServerImpl* owner);

  ~AudioRenderer2Impl() override;

  bool IsOperating();
  bool ValidateConfig();

  AudioServerImpl* owner_ = nullptr;
  f1dl::Binding<AudioRenderer2> audio_renderer_binding_;
  f1dl::BindingSet<AudioRendererGainControl,
                   fbl::unique_ptr<GainControlBinding>> gain_control_bindings_;
  bool is_shutdown_ = false;
  bool gain_events_enabled_ = false;
  fbl::RefPtr<fbl::RefCountedVmoMapper> payload_buffer_;
  bool config_validated_ = false;
};

}  // namespace audio
}  // namespace media
