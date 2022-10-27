// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_CUSTOM_NODE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_CUSTOM_NODE_H_

#include <lib/zx/time.h>
#include <zircon/types.h>

#include <memory>
#include <optional>

#include "fidl/fuchsia.audio.effects/cpp/wire_types.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/fidl/node.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/producer_stage.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"

namespace media_audio {

// A meta node that wraps `CustomStage` with a pre-specified set of child nodes.
class CustomNode : public Node {
 public:
  struct Args {
    // Name of this node.
    std::string_view name;

    // Reference clock of this nodes's destination stream.
    std::shared_ptr<Clock> reference_clock;

    // Whether this node participates in an input pipeline or an output pipeline.
    PipelineDirection pipeline_direction;

    // Process configuration of the custom effect.
    fuchsia_audio_effects::wire::ProcessorConfiguration config;

    // On creation, the node is initially assigned to this detached thread.
    GraphDetachedThreadPtr detached_thread;
  };

  static std::shared_ptr<CustomNode> Create(Args args);

  // Implements `Node`.
  zx::duration PresentationDelayForSourceEdge(const Node* source) const final {
    UNREACHABLE << "PresentationDelayForSourceEdge should not be called on meta nodes";
  }

 private:
  // An ordinary node that wraps the child source node of `CustomNode`.
  class ChildSourceNode : public Node {
   public:
    ChildSourceNode(std::string_view name, PipelineDirection pipeline_direction,
                    PipelineStagePtr pipeline_stage, NodePtr parent,
                    GraphDetachedThreadPtr detached_thread, const Format& format,
                    zx::duration presentation_delay);

    // Implements `Node`.
    zx::duration PresentationDelayForSourceEdge(const Node* source) const final;

   private:
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
    const zx::duration presentation_delay_;
  };

  // An ordinary node that wraps the child destination node of `CustomNode`.
  class ChildDestNode : public Node {
   public:
    ChildDestNode(std::string_view name, PipelineDirection pipeline_direction,
                  PipelineStagePtr pipeline_stage, NodePtr parent,
                  GraphDetachedThreadPtr detached_thread);

    // Implements `Node`.
    zx::duration PresentationDelayForSourceEdge(const Node* source) const final;

   private:
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

  CustomNode(std::string_view name, std::shared_ptr<Clock> reference_clock,
             PipelineDirection pipeline_direction);

  void InitializeChildNodes(PipelineStagePtr pipeline_stage, NodePtr parent,
                            GraphDetachedThreadPtr detached_thread, const Format& source_format,
                            zx::duration presentation_delay);

  NodePtr CreateNewChildSource() final;
  NodePtr CreateNewChildDest() final;
  bool CanAcceptSourceFormat(const Format& format) const final {
    UNREACHABLE << "CanAcceptSourceFormat should not be called on meta nodes";
  }
  std::optional<size_t> MaxSources() const final {
    UNREACHABLE << "MaxSources should not be called on meta nodes";
  }
  bool AllowsDest() const final { UNREACHABLE << "AllowsDest should not be called on meta nodes"; }
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_CUSTOM_NODE_H_
