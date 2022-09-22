// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_PRODUCER_NODE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_PRODUCER_NODE_H_

#include <lib/zx/time.h>
#include <zircon/types.h>

#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/fidl/node.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/producer_stage.h"

namespace media_audio {

class MetaProducerNode;

// This is an ordinary node that wraps a ProducerStage.
class ProducerNode : public Node {
 public:
  struct Args {
    // Name of this node.
    std::string_view name;

    // Whether this node participates in an input pipeline or an output pipeline.
    PipelineDirection pipeline_direction;

    // Parent meta node. Optional.
    NodePtr parent;

    // Command queue for the ProducerStage.
    std::shared_ptr<ProducerStage::CommandQueue> start_stop_command_queue;

    // Internal source for the ProducerStage.
    PipelineStagePtr internal_source;

    // On creation, the node is initially assigned to this DetachedThread.
    DetachedThreadPtr detached_thread;
  };

  static std::shared_ptr<ProducerNode> Create(Args args);

  // Implements `Node`.
  zx::duration GetSelfPresentationDelayForSource(const NodePtr& source) const final;

 private:
  ProducerNode(std::string_view name, PipelineDirection pipeline_direction,
               PipelineStagePtr pipeline_stage, NodePtr parent)
      : Node(name, /*is_meta=*/false, pipeline_stage->reference_clock(), pipeline_direction,
             std::move(pipeline_stage), std::move(parent)) {}

  NodePtr CreateNewChildSource() final {
    UNREACHABLE << "CreateNewChildSource should not be called on ordinary nodes";
  }
  NodePtr CreateNewChildDest() final {
    UNREACHABLE << "CreateNewChildDest should not be called on ordinary nodes";
  }
  bool CanAcceptSourceFormat(const Format& format) const final;
  std::optional<size_t> MaxSources() const final;
  bool AllowsDest() const final;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_PRODUCER_NODE_H_
