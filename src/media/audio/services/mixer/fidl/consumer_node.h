// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_CONSUMER_NODE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_CONSUMER_NODE_H_

#include <zircon/types.h>

#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/fidl/node.h"
#include "src/media/audio/services/mixer/mix/consumer_stage.h"
#include "src/media/audio/services/mixer/mix/thread.h"

namespace media_audio {

// This is an ordinary node that wraps a ConsumerStage.
class ConsumerNode : public Node {
 public:
  struct Args {
    // Name of this node.
    std::string_view name;

    // Whether this node participates in an input pipeline or an output pipeline.
    PipelineDirection pipeline_direction;

    // Format of audio consumed by this node.
    Format format;

    // Reference clock used by this consumer.
    zx_koid_t reference_clock_koid;

    // How to write all consumed packets.
    std::shared_ptr<ConsumerStage::Writer> writer;

    // Which thread the consumer is assigned to.
    ThreadPtr thread;
  };

  static std::shared_ptr<ConsumerNode> Create(Args args);

  // Returns the same object as `pipeline_stage()`, but with a more specialized type.
  ConsumerStagePtr consumer_stage() const { return consumer_stage_; }

  // Starts this consumer.
  void Start(ConsumerStage::StartCommand cmd) const;

  // Stops this consumer.
  void Stop(ConsumerStage::StopCommand cmd) const;

  // Implements `Node`.
  zx::duration GetSelfPresentationDelayForSource(const NodePtr& source) const final;

 private:
  using CommandQueue = ConsumerStage::CommandQueue;

  ConsumerNode(std::string_view name, PipelineDirection pipeline_direction,
               ConsumerStagePtr pipeline_stage, const Format& format,
               std::shared_ptr<CommandQueue> command_queue)
      : Node(name, /*is_meta=*/false, pipeline_stage->reference_clock_koid(), pipeline_direction,
             pipeline_stage, /*parent=*/nullptr),
        format_(format),
        command_queue_(std::move(command_queue)),
        consumer_stage_(std::move(pipeline_stage)) {}

  // Implementation of Node.
  NodePtr CreateNewChildSource() final {
    UNREACHABLE << "CreateNewChildSource should not be called on ordinary nodes";
  }
  NodePtr CreateNewChildDest() final {
    UNREACHABLE << "CreateNewChildDest should not be called on ordinary nodes";
  }
  bool CanAcceptSourceFormat(const Format& format) const final;
  std::optional<size_t> MaxSources() const final;
  bool AllowsDest() const final;

  const Format format_;
  const std::shared_ptr<CommandQueue> command_queue_;
  const ConsumerStagePtr consumer_stage_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_CONSUMER_NODE_H_
