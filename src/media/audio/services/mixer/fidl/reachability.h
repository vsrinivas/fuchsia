// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REACHABILITY_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REACHABILITY_H_

#include <lib/zx/time.h>

#include <unordered_map>
#include <unordered_set>

#include "src/media/audio/services/mixer/fidl/node.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"

namespace media_audio {

// Computes the total downstream delay starting from the edge `source` -> `node`. If `source` is
// nullptr, this typically implies that `node` is a producer node where the downstream delay does
// not depend on an incoming source node.
//
// REQUIRED: node.type() != Node::Type::kMeta
// REQUIRED: if source != nullptr, then source is in node->sources()
zx::duration ComputeDownstreamDelay(const Node& node, const Node* source);

// Computes the total upstream delay starting from a given `node` in a mix graph. This includes the
// delay added by `node`, plus the delay from all incoming paths.
//
// REQUIRED: node.type() != Node::Type::kMeta
zx::duration ComputeUpstreamDelay(const Node& node);

// Reports whether there exists a path from `source` to `dest`. The nodes may be ordinary nodes
// and/or meta nodes. For any given meta node M, there are implicit paths from M's child source
// nodes, to M itself, to M's child destination nodes. That is, given:
//
// ```
//                A
//                |
//     +----------V-----------+
//     |        +---+       M |
//     |        | I |         |   // M.child_sources()
//     |        +---+         |
//     | +----+ +----+ +----+ |
//     | | O1 | | O2 | | O3 | |   // M.child_dests()
//     | +----+ +----+ +----+ |
//     +---|------|------|----+
//         |      |      |
//         V      V      V
//         B      C      D
// ```
//
// There exists paths:
//
// ```
// A -> I -> M -> O1 -> B
// A -> I -> M -> O2 -> C
// A -> I -> M -> O3 -> D
// ```
bool ExistsPath(const Node& source, const Node& dest);

// Moves `node` and its source tree to `thread`, where `node` is assumed to be currently attached to
// `expected_thread`. A node's "source tree" is the set of upstream nodes n ∈ N such that there
// exists a path from n to `node` that does not go through a consumer node.
//
// For example, in the following diagram:
//
// ```
//        A
//        |
//        V
//  +------------+
//  |     C      |
//  |  splitter  |
//  | P1  P2  P3 |
//  +------------+        H
//    |   |   |           |
//    V   V   V           V
//    D   E   F           G
//            |           |
//            +-----+-----+
//                  |
//                  V
//                  N
// ```
//
// If C is a consumer node, then `MoveNodetoThread(N, new_thread, old_thread)` will move the
// following nodes to new_thread: {N, F, G, P3, H}. This must be a tree: by construction, all
// fan-out must happen below a consumer node, as in the splitter example above.
//
// Before a node is moved to `new_thread`, we check that the node is currently attached to
// `expected_thread`. We will crash if this expectation is not satisfied.
//
// Returns the set of PipelineStages that must move to `new_thread->pipeline_thread()`.
//
// REQUIRED: node.type() != Node::Type::kMeta
std::vector<PipelineStagePtr> MoveNodeToThread(Node& node, std::shared_ptr<GraphThread> new_thread,
                                               std::shared_ptr<GraphThread> expected_thread);

// Recomputes the maximum number of downstream consumers at `node`. This is recomputed
// incrementally, assuming the count is already correct for all of the node's outgoing edges. If the
// count has changed at `node`, we recompute the count for all nodes on all paths incoming to
// `node`.
//
// The return value is the set of PipelineStages whose count has changed. PipelineStages are grouped
// by thread ID to simplify how this result is used in node.cc.
//
// REQUIRED: node.type() != Node::Type::kMeta
std::unordered_map<ThreadId, std::unordered_map<PipelineStagePtr, int64_t>>
RecomputeMaxDownstreamConsumers(Node& node);

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REACHABILITY_H_
