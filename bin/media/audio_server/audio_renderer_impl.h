// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <set>

#include "lib/media/flog/flog.h"
#include "lib/media/timeline/timeline_function.h"
#include "lib/media/timeline/timeline_rate.h"
#include "lib/media/fidl/audio_renderer.fidl.h"
#include "lib/media/fidl/logs/media_renderer_channel.fidl.h"
#include "lib/media/fidl/media_renderer.fidl.h"
#include "garnet/bin/media/audio_server/audio_pipe.h"
#include "garnet/bin/media/audio_server/fwd_decls.h"
#include "garnet/bin/media/util/timeline_control_point.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace media {
namespace audio {

class AudioRendererImpl : public AudioRenderer, public MediaRenderer {
 public:
  // TODO(johngro): Find a better place for this constant.  It affects the
  // behavior of more than just the Audio Renderer implementation.
  static constexpr size_t PTS_FRACTIONAL_BITS = 12;

  ~AudioRendererImpl() override;
  static AudioRendererImplPtr Create(
      fidl::InterfaceRequest<AudioRenderer> audio_renderer_request,
      fidl::InterfaceRequest<MediaRenderer> media_renderer_request,
      AudioServerImpl* owner);

  // Shutdown the audio renderer, unlinking it from all outputs, closing
  // connections to all clients and removing it from its owner server's list.
  void Shutdown();

  // Methods used by the output manager to link/unlink this renderer to/from
  // different outputs.
  void AddOutput(AudioRendererToOutputLinkPtr link);
  void RemoveOutput(AudioRendererToOutputLinkPtr link);
  void RemoveAllOutputs();
  void SetThrottleOutput(
      const AudioRendererToOutputLinkPtr& throttle_output_link);

  // Used by the output to report packet usage.
  void OnRenderRange(int64_t presentation_time, uint32_t duration);

  // Note: format_info() is subject to change and must only be accessed from the
  // main message loop thread.  Outputs which are running on mixer threads
  // should never access format_info() directly from a renderer.  Instead, they
  // should use the format_info which was assigned to the RendererToOutput link
  // at the time the link was created.
  const fbl::RefPtr<AudioRendererFormatInfo>& format_info() const {
    return format_info_;
  }
  bool format_info_valid() const { return (format_info_ != nullptr); }

  float db_gain() const { return db_gain_; }
  TimelineControlPoint& timeline_control_point() {
    return timeline_control_point_;
  }

 private:
  friend class AudioPipe;

  AudioRendererImpl(
      fidl::InterfaceRequest<AudioRenderer> audio_renderer_request,
      fidl::InterfaceRequest<MediaRenderer> media_renderer_request,
      AudioServerImpl* owner);

  // Implementation of AudioRenderer interface.
  void SetGain(float db_gain) override;
  void GetMinDelay(const GetMinDelayCallback& callback) override;

  // MediaRenderer implementation.
  void GetSupportedMediaTypes(
      const GetSupportedMediaTypesCallback& callback) override;
  void SetMediaType(MediaTypePtr media_type) override;
  void GetPacketConsumer(
      fidl::InterfaceRequest<MediaPacketConsumer> consumer_request) override;
  void GetTimelineControlPoint(fidl::InterfaceRequest<MediaTimelineControlPoint>
                                   control_point_request) override;

  // Methods called by our AudioPipe.
  //
  // TODO(johngro): MI is banned by style, but multiple interface inheritance
  // (inheriting for one or more base classes consisting only of pure virtual
  // methods) is allowed.  Consider defining an interface for AudioPipe
  // encapsulation so that AudioPipe does not have to know that we are an
  // AudioRendererImpl (just that we implement its interface).
  void OnPacketReceived(AudioPipe::AudioPacketRefPtr packet);
  bool OnFlushRequested(const MediaPacketConsumer::FlushCallback& cbk);
  fidl::Array<MediaTypeSetPtr> SupportedMediaTypes();

  AudioRendererImplWeakPtr weak_this_;
  AudioServerImpl* owner_;
  fidl::Binding<AudioRenderer> audio_renderer_binding_;
  fidl::Binding<MediaRenderer> media_renderer_binding_;
  AudioPipe pipe_;
  TimelineControlPoint timeline_control_point_;
  fbl::RefPtr<AudioRendererFormatInfo> format_info_;
  AudioRendererToOutputLinkSet output_links_;
  AudioRendererToOutputLinkPtr throttle_output_link_;
  float db_gain_ = 0.0;
  bool is_shutdown_ = false;

  FLOG_INSTANCE_CHANNEL(logs::MediaRendererChannel, log_channel_);
};

}  // namespace audio
}  // namespace media
