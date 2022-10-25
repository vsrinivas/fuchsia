// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_SPLITTER_NODE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_SPLITTER_NODE_H_

#include <lib/zx/time.h>

#include <memory>

#include "src/media/audio/lib/clock/clock.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/fidl/graph_detached_thread.h"
#include "src/media/audio/services/mixer/fidl/graph_mix_thread.h"
#include "src/media/audio/services/mixer/fidl/node.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/ring_buffer.h"
#include "src/media/audio/services/mixer/mix/splitter_consumer_stage.h"
#include "src/media/audio/services/mixer/mix/splitter_producer_stage.h"

namespace media_audio {

// A SplitterNode implements fan-out: an incoming audio stream is fed into a consumer, which copies
// that stream into a RingBuffer, which is read by outgoing producers, as illustrated below:
//
// ```
//                A
//                |
//     +----------V-----------+
//     |        +---+ Splitter|
//     |        | C |         |   // Splitter.child_sources()
//     |        +-|-+         |
//     |          V           |
//     |     ring buffer      |
//     |     |    |     |     |
//     | +---V+ +-V--+ +V---+ |
//     | | P1 | | P2 | | P3 | |   // Splitter.child_dests()
//     | +----+ +----+ +----+ |
//     +---|------|------|----+
//         |      |      |
//         V      V      V
//         B      C      D
// ```
class SplitterNode : public Node, public std::enable_shared_from_this<SplitterNode> {
 public:
  struct Args {
    // Name of this node.
    std::string_view name;

    // Whether this node participates in an input pipeline or an output pipeline.
    PipelineDirection pipeline_direction;

    // Format of data consumed and produced by this node.
    Format format;

    // Reference clock of this nodes's source and destination streams.
    std::shared_ptr<Clock> reference_clock;

    // Which thread the consumer node is assigned to.
    std::shared_ptr<GraphMixThread> consumer_thread;

    // On creation, child ProducerNodes are initially assigned to this detached thread.
    GraphDetachedThreadPtr detached_thread;
  };

  static std::shared_ptr<SplitterNode> Create(Args args);

  // Implements `Node`.
  zx::duration GetSelfPresentationDelayForSource(const Node* source) const final {
    UNREACHABLE << "GetSelfPresentationDelayForSource should not be called on meta nodes";
  }

 private:
  // The type of node placed in `Splitter.child_sources()`.
  class ChildConsumerNode : public Node {
   public:
    struct Args {
      std::string_view name;
      std::shared_ptr<SplitterNode> parent;
      std::shared_ptr<SplitterConsumerStage> pipeline_stage;
    };
    explicit ChildConsumerNode(Args args);

    // Implements `Node`.
    zx::duration GetSelfPresentationDelayForSource(const Node* source) const final;

    // Overrides `Node::pipeline_stage` with a more specific type.
    [[nodiscard]] std::shared_ptr<SplitterConsumerStage> pipeline_stage() const {
      return std::static_pointer_cast<SplitterConsumerStage>(Node::pipeline_stage());
    }

   private:
    NodePtr CreateNewChildSource() final {
      UNREACHABLE << "CreateNewChildSource should not be called on ordinary nodes";
    }
    NodePtr CreateNewChildDest() final {
      UNREACHABLE << "CreateNewChildDest should not be called on ordinary nodes";
    }
    bool CanAcceptSourceFormat(const Format& format) const final {
      return format == pipeline_stage()->format();
    }
    std::optional<size_t> MaxSources() const final { return 1; }
    bool AllowsDest() const final { return false; }
  };

  // The type of node placed in `Splitter.child_dests()`.
  class ChildProducerNode : public Node {
   public:
    struct Args {
      std::string_view name;
      std::shared_ptr<SplitterNode> parent;
      std::shared_ptr<SplitterProducerStage> pipeline_stage;
    };
    explicit ChildProducerNode(Args args);

    // Implements `Node`.
    zx::duration GetSelfPresentationDelayForSource(const Node* source) const final;

    // Overrides `Node::pipeline_stage` with a more specific type.
    [[nodiscard]] std::shared_ptr<SplitterProducerStage> pipeline_stage() const {
      return std::static_pointer_cast<SplitterProducerStage>(Node::pipeline_stage());
    }

   private:
    NodePtr CreateNewChildSource() final {
      UNREACHABLE << "CreateNewChildSource should not be called on ordinary nodes";
    }
    NodePtr CreateNewChildDest() final {
      UNREACHABLE << "CreateNewChildDest should not be called on ordinary nodes";
    }
    bool CanAcceptSourceFormat(const Format& format) const final { return false; }
    std::optional<size_t> MaxSources() const final { return 0; }
    bool AllowsDest() const final { return true; }
  };

  SplitterNode(Args args, std::shared_ptr<RingBuffer> ring_buffer);

  NodePtr CreateNewChildSource() final;
  NodePtr CreateNewChildDest() final;
  void DestroySelf() final;

  bool CanAcceptSourceFormat(const Format& format) const final {
    UNREACHABLE << "CanAcceptSourceFormat should not be called on meta nodes";
  }
  std::optional<size_t> MaxSources() const final {
    UNREACHABLE << "MaxSources should not be called on meta nodes";
  }
  bool AllowsDest() const final { UNREACHABLE << "AllowsDest should not be called on meta nodes"; }

  const Format format_;
  const GraphDetachedThreadPtr detached_thread_;
  const std::shared_ptr<RingBuffer> ring_buffer_;

  // This is logically const, but can't be created until after the SplitterNode is created due to
  // a circular dependency. This is set by Create then not changed until DestroySelf.
  std::shared_ptr<ChildConsumerNode> consumer_;

  // For creating ChildProducerNode names.
  int64_t num_producers_created_ = 0;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_SPLITTER_NODE_H_
