// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/producer_node.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"

namespace media_audio {

// static
std::shared_ptr<ProducerNode> ProducerNode::Create(Args args) {
  struct WithPublicCtor : public ProducerNode {
   public:
    explicit WithPublicCtor(std::string_view name, PipelineStagePtr pipeline_stage, NodePtr parent)
        : ProducerNode(name, std::move(pipeline_stage), std::move(parent)) {}
  };

  auto pipeline_stage = std::make_shared<ProducerStage>(ProducerStage::Args{
      .name = args.name,
      .format = args.internal_source->format(),
      .reference_clock_koid = args.internal_source->reference_clock_koid(),
      .command_queue = std::move(args.start_stop_command_queue),
      .internal_source = std::move(args.internal_source),
  });
  pipeline_stage->set_thread(args.detached_thread);

  auto node = std::make_shared<WithPublicCtor>(args.name, std::move(pipeline_stage),
                                               std::move(args.parent));
  node->set_pipeline_stage_thread(args.detached_thread);
  return node;
}

zx::duration ProducerNode::GetSelfPresentationDelayForSource(const NodePtr& source) {
  // Producers do not have internal delay contribution.
  // TODO(fxbug.dev/87651): Add a method to introduce external delay.
  return zx::duration(0);
}

bool ProducerNode::CanAcceptSource(NodePtr source) const {
  // Producers do not have sources.
  return false;
}

}  // namespace media_audio
