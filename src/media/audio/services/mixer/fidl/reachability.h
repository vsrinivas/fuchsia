// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REACHABILITY_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REACHABILITY_H_

#include "src/media/audio/services/mixer/fidl/node.h"

namespace media_audio {

// Reports whether there exists a path from `src` to `dest`. The nodes may be ordinary nodes and/or
// meta nodes. For any given meta node M, there are implicit paths from M's child source nodes, to M
// itself, to M's child destination nodes. That is, given:
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
// There exists a path `A -> M`, `M -> B`, and `A -> B`.
bool ExistsPath(const Node& src, const Node& dest);

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REACHABILITY_H_
