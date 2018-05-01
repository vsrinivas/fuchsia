// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/framework/models/node.h"

#include "garnet/bin/media/media_player/framework/formatting.h"

namespace media_player {

const char* GenericNode::label() const {
  return "<not labelled>";
}

void GenericNode::Dump(std::ostream& os, NodeRef ref) const {
  os << label();
  DumpDownstreamNodes(os, ref);
}

void GenericNode::PostTask(const fxl::Closure& task) {
  Stage* generic_stage = generic_stage_;
  if (generic_stage) {
    generic_stage->PostTask(task);
  }
}

void GenericNode::DumpDownstreamNodes(std::ostream& os, NodeRef ref) const {
  for (size_t output_index = 0; output_index < ref.output_count();
       output_index++) {
    os << "\n" << begl << "[" << output_index << "] " << indent;
    NodeRef downstream_node = ref.output(output_index).mate().node();
    downstream_node.GetGenericNode()->Dump(os, downstream_node);
    os << outdent;
  }
}

}  // namespace media_player
