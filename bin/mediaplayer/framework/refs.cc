// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/framework/refs.h"

#include "garnet/bin/mediaplayer/framework/stages/input.h"
#include "garnet/bin/mediaplayer/framework/stages/output.h"
#include "garnet/bin/mediaplayer/framework/stages/stage_impl.h"
#include "lib/fxl/logging.h"

namespace media_player {

size_t NodeRef::input_count() const {
  FXL_DCHECK(stage_);
  return stage_->input_count();
}

InputRef NodeRef::input(size_t index) const {
  FXL_DCHECK(stage_);
  FXL_DCHECK(index < stage_->input_count());
  return InputRef(&stage_->input(index));
}

InputRef NodeRef::input() const {
  FXL_DCHECK(stage_);
  FXL_DCHECK(stage_->input_count() == 1);
  return InputRef(&stage_->input(0));
}

size_t NodeRef::output_count() const {
  FXL_DCHECK(stage_);
  return stage_->output_count();
}

OutputRef NodeRef::output(size_t index) const {
  FXL_DCHECK(stage_);
  FXL_DCHECK(index < stage_->output_count());
  return OutputRef(&stage_->output(index));
}

OutputRef NodeRef::output() const {
  FXL_DCHECK(stage_);
  FXL_DCHECK(stage_->output_count() == 1);
  return OutputRef(&stage_->output(0));
}

GenericNode* NodeRef::GetGenericNode() {
  return stage_ ? stage_->GetGenericNode() : nullptr;
}

NodeRef InputRef::node() const {
  return input_ ? NodeRef(input_->stage()) : NodeRef();
}

bool InputRef::connected() const {
  FXL_DCHECK(input_);
  return input_->connected();
}

bool InputRef::prepared() const {
  FXL_DCHECK(input_);
  return input_->prepared();
}

OutputRef InputRef::mate() const {
  FXL_DCHECK(input_);
  return OutputRef(input_->mate());
}

NodeRef OutputRef::node() const {
  return output_ ? NodeRef(output_->stage()) : NodeRef();
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
