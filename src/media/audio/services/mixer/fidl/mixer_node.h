// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_MIXER_NODE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_MIXER_NODE_H_

#include <memory>
#include <optional>
#include <string_view>

#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/fidl/node.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"

namespace media_audio {

// An ordinary node that wraps `MixerStage`.
class MixerNode : public Node {
 public:
  struct Args {
    // Name of this node.
    std::string_view name;

    // Whether this node participates in an input pipeline or an output pipeline.
    PipelineDirection pipeline_direction;

    // Format of audio consumed by this node.
    Format format;

    // Reference clock of this nodes's destination stream.
    std::shared_ptr<Clock> reference_clock;

    // Size of the internal mix buffer.
    int64_t dest_buffer_frame_count;

    // On creation, the node is initially assigned to this detached thread.
    GraphDetachedThreadPtr detached_thread;
  };

  static std::shared_ptr<MixerNode> Create(Args args);

  // Implements `Node`.
  zx::duration GetSelfPresentationDelayForSource(const Node* source) const final;

 private:
  MixerNode(std::string_view name, std::shared_ptr<Clock> reference_clock,
            PipelineDirection pipeline_direction, PipelineStagePtr pipeline_stage);

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

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_MIXER_NODE_H_
