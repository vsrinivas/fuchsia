// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/mixer_node.h"

#include <lib/zx/time.h>

#include <limits>
#include <memory>

#include "src/media/audio/lib/clock/unreadable_clock.h"
#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/processing/filter.h"
#include "src/media/audio/services/mixer/mix/mixer_stage.h"

namespace media_audio {

namespace {

// Adds padding to `delay` to compensate for the clock rate differences. Since the clock rates can
// differ up to +/-1000PPM, we can scale the delay amounts by adding a padding of the maximum
// possible difference 2000PPM. This is simpler than fetching and calculating the exact rate
// differences. Considering that the delays are computed and used as bounding numbers, we do not
// need any further degree of accuracy here.
zx::duration DelayWithPadding(zx::duration delay) { return delay * 1002 / 1000; }

}  // namespace

std::shared_ptr<MixerNode> MixerNode::Create(Args args) {
  auto pipeline_stage = std::make_shared<MixerStage>(
      args.name, args.format, UnreadableClock(args.reference_clock), args.dest_buffer_frame_count);
  pipeline_stage->set_thread(args.detached_thread->pipeline_thread());

  struct WithPublicCtor : public MixerNode {
   public:
    explicit WithPublicCtor(std::string_view name, std::shared_ptr<Clock> reference_clock,
                            PipelineDirection pipeline_direction, PipelineStagePtr pipeline_stage)
        : MixerNode(name, std::move(reference_clock), pipeline_direction,
                    std::move(pipeline_stage)) {}
  };
  auto node = std::make_shared<WithPublicCtor>(args.name, std::move(args.reference_clock),
                                               args.pipeline_direction, std::move(pipeline_stage));
  node->set_thread(args.detached_thread);
  return node;
}

zx::duration MixerNode::PresentationDelayForSourceEdge(const Node* source) const {
  FX_CHECK(source);
  const auto& dest_format = pipeline_stage()->format();
  const int32_t source_frame_rate =
      static_cast<int32_t>(source->pipeline_stage()->format().frames_per_second());
  const int32_t dest_frame_rate = static_cast<int32_t>(dest_format.frames_per_second());
  // TODO(fxbug.dev/114373): Handle the case where the sampler type is explicitly chosen in the FIDL
  // API - this may require to access the actual sampler being used for this source.
  const Fixed delay_frames = (source_frame_rate == dest_frame_rate)
                                 ? kHalfFrame
                                 : SincFilter::Length(source_frame_rate, dest_frame_rate);
  const zx::duration delay = zx::nsec(dest_format.frames_per_ns().Inverse().Scale(
      delay_frames.Ceiling(), TimelineRate::RoundingMode::Ceiling));
  return (reference_clock() == source->reference_clock()) ? delay : DelayWithPadding(delay);
}

MixerNode::MixerNode(std::string_view name, std::shared_ptr<Clock> reference_clock,
                     PipelineDirection pipeline_direction, PipelineStagePtr pipeline_stage)
    : Node(Type::kMixer, name, std::move(reference_clock), pipeline_direction,
           std::move(pipeline_stage), /*parent=*/nullptr) {}

bool MixerNode::CanAcceptSourceFormat(const Format& format) const { return true; }

std::optional<size_t> MixerNode::MaxSources() const {
  // TODO(fxbug.dev/87651): Define a concrete limit here (and in the FIDL API) - perhaps repurpose
  // `fuchsia::audio.effects::MAX_INPUT_STREAMS`?
  return std::numeric_limits<size_t>::max();
}

bool MixerNode::AllowsDest() const { return true; }

}  // namespace media_audio
