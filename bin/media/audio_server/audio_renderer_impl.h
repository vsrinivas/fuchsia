// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <set>

#include "apps/media/lib/timeline/timeline_function.h"
#include "apps/media/lib/timeline/timeline_rate.h"
#include "apps/media/services/audio_renderer.fidl.h"
#include "apps/media/services/media_renderer.fidl.h"
#include "apps/media/src/audio_server/audio_pipe.h"
#include "apps/media/src/audio_server/fwd_decls.h"
#include "apps/media/src/util/timeline_control_point.h"
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
  void AddOutput(AudioRendererToOutputLinkPtr link,
                 const AudioOutputPtr& throttle_output);
  void RemoveOutput(AudioRendererToOutputLinkPtr link);

  // Accessors used by AudioOutputs during mixing to access parameters which are
  // important for the mixing process.
  void SnapshotRateTrans(TimelineFunction* out, uint32_t* generation = nullptr);

  TimelineRate FractionalFrameToMediaTimeRatio() const {
    return frame_to_media_ratio_;
  }

  uint32_t BytesPerFrame() const { return bytes_per_frame_; }
  const AudioMediaTypeDetailsPtr& Format() const { return format_; }
  float DbGain() const { return db_gain_; }

 private:
  friend class AudioPipe;

  AudioRendererImpl(
      fidl::InterfaceRequest<AudioRenderer> audio_renderer_request,
      fidl::InterfaceRequest<MediaRenderer> media_renderer_request,
      AudioServerImpl* owner);

  // Implementation of AudioRenderer interface.
  void SetGain(float db_gain) override;

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

  AudioRendererImplWeakPtr weak_this_;
  AudioServerImpl* owner_;
  fidl::Binding<AudioRenderer> audio_renderer_binding_;
  fidl::Binding<MediaRenderer> media_renderer_binding_;
  AudioPipe pipe_;
  TimelineControlPoint timeline_control_point_;
  TimelineRate frames_per_ns_;
  TimelineRate frame_to_media_ratio_;
  uint32_t bytes_per_frame_ = 1;
  AudioMediaTypeDetailsPtr format_;
  AudioRendererToOutputLinkSet outputs_;
  float db_gain_ = 0.0;
};

}  // namespace audio
}  // namespace media
