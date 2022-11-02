// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REACHABILITY_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REACHABILITY_H_

#include <lib/zx/time.h>

#include <map>
#include <unordered_set>
#include <vector>

#include "src/media/audio/services/mixer/fidl/node.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"

namespace media_audio {

// Recomputes delays at `node`. These are recomputed incrementally:
//
// * For downstream delays, we assume the delays are already correct for all of `node`'s outgoing
//   edges. If a downstream delay has changed at `node`, we recurse on `node`'s incoming edges.
//
// * For upstream delays, we assume the delays are already correct for all of `node`'s incoming
//   edges. If an upstream delay has changed at `node`, we recurse on `node`'s outgoing edges.
//
// If any delays change, the `closures` mapping is updated with a set of (ThreadId, closure) pairs,
// where `closure` should be run on `ThreadId`. These closures copy the delay changes into state on
// mix threads, such as state in ConsumerStage. This is a `map` instead of an `unordered_map` so the
// caller can process the ThreadIds in a deterministic order (which is helpful in tests).
//
// REQUIRED: the property to compute is defined at `node`
void RecomputeMaxDownstreamDelays(Node& node,
                                  std::map<ThreadId, std::vector<fit::closure>>& closures);

void RecomputeMaxUpstreamDelays(Node& node,
                                std::map<ThreadId, std::vector<fit::closure>>& closures);

// Call the above functions assuming that the edge `source -> dest` was just created or deleted.
std::map<ThreadId, std::vector<fit::closure>> RecomputeDelays(Node& source, Node& dest);

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
// exists a path from each n to `node`, where the path does not go through a consumer node.
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
// If C has type `Node::Type::kConsumer`, then `MoveNodetoThread(N, new_thread, old_thread)` will
// move the following nodes to `new_thread`: {N, F, G, P3, H}. [By
// construction](../docs/execution_model.md), this set of nodes must form a tree rooted at N.
//
// Before a node is moved to `new_thread`, we check that the node is currently attached to
// `expected_thread`. We will crash if this expectation is not satisfied.
//
// Returns the set of PipelineStages that must move to `new_thread->pipeline_thread()`.
//
// REQUIRED: node.type() != Node::Type::kMeta
std::vector<PipelineStagePtr> MoveNodeToThread(Node& node, std::shared_ptr<GraphThread> new_thread,
                                               std::shared_ptr<GraphThread> expected_thread);

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REACHABILITY_H_
