// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REACHABILITY_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REACHABILITY_H_

#include <lib/zx/time.h>

#include "src/media/audio/services/mixer/fidl/node.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"

namespace media_audio {

// Computes the total downstream delay starting from the edge `source` -> `node`. If `source` is
// nullptr, this typically implies that `node` is a producer node where the downstream delay does
// not depend on an incoming source node.
//
// REQUIRED: !node.is_meta()
// REQUIRED: if source != nullptr, then source is in node->sources()
zx::duration ComputeDownstreamDelay(const Node& node, const Node* source);

// Computes the total upstream delay starting from a given `node` in a mix graph. This includes the
// delay added by `node`, plus the delay from all incoming paths.
//
// REQUIRED: !node.is_meta()
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

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REACHABILITY_H_
