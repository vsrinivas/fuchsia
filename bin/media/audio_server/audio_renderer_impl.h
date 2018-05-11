// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/audio_server/audio_link_packet_source.h"
#include "garnet/bin/media/audio_server/audio_object.h"
#include "garnet/bin/media/audio_server/audio_renderer_format_info.h"
#include "lib/media/timeline/timeline_function.h"

namespace media {
namespace audio {

class AudioRendererImpl
    : public AudioObject,
      public fbl::DoublyLinkedListable<fbl::RefPtr<AudioRendererImpl>> {
 public:
  virtual void Shutdown() = 0;

  // Used by the output to report packet usage.
  virtual void OnRenderRange(int64_t presentation_time, uint32_t duration) = 0;

  virtual void SnapshotCurrentTimelineFunction(
      int64_t reference_time, TimelineFunction* out,
      uint32_t* generation = nullptr) = 0;

  void SetThrottleOutput(
      std::shared_ptr<AudioLinkPacketSource> throttle_output_link);

  // Recompute the minimum clock lead time based on the current set of outputs
  // we are linked to.  If this requirement is different from the previous
  // requirement, report it to our users (if they care).
  void RecomputeMinClockLeadTime();

  // Note: format_info() is subject to change and must only be accessed from the
  // main message loop thread.  Outputs which are running on mixer threads
  // should never access format_info() directly from a renderer.  Instead, they
  // should use the format_info which was assigned to the AudioLink at the time
  // the link was created.
  const fbl::RefPtr<AudioRendererFormatInfo>& format_info() const {
    return format_info_;
  }
  bool format_info_valid() const { return (format_info_ != nullptr); }

  float db_gain() const { return db_gain_; }

 protected:
  AudioRendererImpl();

  virtual void ReportNewMinClockLeadTime() {}

  fbl::RefPtr<AudioRendererFormatInfo> format_info_;
  float db_gain_ = 0.0;
  bool mute_ = false;
  std::shared_ptr<AudioLinkPacketSource> throttle_output_link_;

  // Minimum Clock Lead Time state
  int64_t min_clock_lead_nsec_ = 0;
};

}  // namespace audio
}  // namespace media
