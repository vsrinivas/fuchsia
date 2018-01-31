// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <fbl/ref_ptr.h>
#include <set>

#include "garnet/bin/media/audio_server/audio_link_packet_source.h"
#include "garnet/bin/media/audio_server/audio_object.h"
#include "garnet/bin/media/audio_server/audio_packet_ref.h"
#include "garnet/bin/media/audio_server/audio_pipe.h"
#include "garnet/bin/media/audio_server/fwd_decls.h"
#include "garnet/bin/media/audio_server/audio_renderer_impl.h"
#include "garnet/bin/media/util/timeline_control_point.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/media/fidl/audio_renderer.fidl.h"
#include "lib/media/fidl/logs/media_renderer_channel.fidl.h"
#include "lib/media/fidl/media_renderer.fidl.h"
#include "lib/media/flog/flog.h"
#include "lib/media/timeline/timeline_function.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media {
namespace audio {

class AudioRenderer1Impl :
      public AudioRendererImpl,
      public AudioRenderer,
      public MediaRenderer {
 public:
  ~AudioRenderer1Impl() override;
  static fbl::RefPtr<AudioRenderer1Impl> Create(
      f1dl::InterfaceRequest<AudioRenderer> audio_renderer_request,
      f1dl::InterfaceRequest<MediaRenderer> media_renderer_request,
      AudioServerImpl* owner);

  // Shutdown the audio renderer, unlinking it from all outputs, closing
  // connections to all clients and removing it from its owner server's list.
  void Shutdown() override;

  // Used by the output to report packet usage.
  void OnRenderRange(int64_t presentation_time, uint32_t duration) override;

  virtual void SnapshotCurrentTimelineFunction(
      int64_t reference_time,
      TimelineFunction* out,
      uint32_t* generation) override;

 private:
  friend class AudioPipe;

  AudioRenderer1Impl(
      f1dl::InterfaceRequest<AudioRenderer> audio_renderer_request,
      f1dl::InterfaceRequest<MediaRenderer> media_renderer_request,
      AudioServerImpl* owner);

  // AudioObject overrides.
  zx_status_t InitializeDestLink(const AudioLinkPtr& link) final;

  // Implementation of AudioRenderer interface.
  void SetGain(float db_gain) override;
  void GetMinDelay(const GetMinDelayCallback& callback) override;

  // MediaRenderer implementation.
  void GetSupportedMediaTypes(
      const GetSupportedMediaTypesCallback& callback) override;
  void SetMediaType(MediaTypePtr media_type) override;
  void GetPacketConsumer(
      f1dl::InterfaceRequest<MediaPacketConsumer> consumer_request) override;
  void GetTimelineControlPoint(f1dl::InterfaceRequest<MediaTimelineControlPoint>
                                   control_point_request) override;

  // Methods called by our AudioPipe.
  //
  // TODO(johngro): MI is banned by style, but multiple interface inheritance
  // (inheriting for one or more base classes consisting only of pure virtual
  // methods) is allowed.  Consider defining an interface for AudioPipe
  // encapsulation so that AudioPipe does not have to know that we are an
  // AudioRenderer1Impl (just that we implement its interface).
  void OnPacketReceived(fbl::RefPtr<AudioPacketRef> packet);
  bool OnFlushRequested(const MediaPacketConsumer::FlushCallback& cbk);
  f1dl::Array<MediaTypeSetPtr> SupportedMediaTypes();

  AudioServerImpl* owner_;
  f1dl::Binding<AudioRenderer> audio_renderer_binding_;
  f1dl::Binding<MediaRenderer> media_renderer_binding_;
  AudioPipe pipe_;
  TimelineControlPoint timeline_control_point_;
  bool is_shutdown_ = false;

  FLOG_INSTANCE_CHANNEL(logs::MediaRendererChannel, log_channel_);
};

}  // namespace audio
}  // namespace media
