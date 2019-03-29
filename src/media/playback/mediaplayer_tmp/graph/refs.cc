// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer_tmp/graph/refs.h"

#include "src/lib/fxl/logging.h"
#include "src/media/playback/mediaplayer_tmp/graph/nodes/input.h"
#include "src/media/playback/mediaplayer_tmp/graph/nodes/node.h"
#include "src/media/playback/mediaplayer_tmp/graph/nodes/output.h"

namespace media_player {

size_t NodeRef::input_count() const {
  FXL_DCHECK(node_);
  return node_->input_count();
}

InputRef NodeRef::input(size_t index) const {
  FXL_DCHECK(node_);
  FXL_DCHECK(index < node_->input_count());
  return InputRef(&node_->input(index));
}

InputRef NodeRef::input() const {
  FXL_DCHECK(node_);
  FXL_DCHECK(node_->input_count() == 1);
  return InputRef(&node_->input(0));
}

size_t NodeRef::output_count() const {
  FXL_DCHECK(node_);
  return node_->output_count();
}

OutputRef NodeRef::output(size_t index) const {
  FXL_DCHECK(node_);
  FXL_DCHECK(index < node_->output_count());
  return OutputRef(&node_->output(index));
}

OutputRef NodeRef::output() const {
  FXL_DCHECK(node_);
  FXL_DCHECK(node_->output_count() == 1);
  return OutputRef(&node_->output(0));
}

NodeRef InputRef::node() const {
  return input_ ? NodeRef(input_->node()) : NodeRef();
}

bool InputRef::connected() const {
  FXL_DCHECK(input_);
  return input_->connected();
}

OutputRef InputRef::mate() const {
  FXL_DCHECK(input_);
  return OutputRef(input_->mate());
}

NodeRef OutputRef::node() const {
  return output_ ? NodeRef(output_->node()) : NodeRef();
}

bool OutputRef::connected() const {
  FXL_DCHECK(output_);
  return output_->connected();
}

InputRef OutputRef::mate() const {
  FXL_DCHECK(output_);
  return InputRef(output_->mate());
}

}  // namespace media_player
