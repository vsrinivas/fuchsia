// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/refs.h"

#include "apps/media/src/framework/stages/input.h"
#include "apps/media/src/framework/stages/output.h"
#include "apps/media/src/framework/stages/stage.h"
#include "lib/ftl/logging.h"

namespace media {

size_t NodeRef::input_count() const {
  FTL_DCHECK(valid());
  return stage_->input_count();
}

InputRef NodeRef::input(size_t index) const {
  FTL_DCHECK(valid());
  FTL_DCHECK(index < stage_->input_count());
  return InputRef(stage_, index);
}

InputRef NodeRef::input() const {
  FTL_DCHECK(valid());
  FTL_DCHECK(stage_->input_count() == 1);
  return InputRef(stage_, 0);
}

size_t NodeRef::output_count() const {
  FTL_DCHECK(valid());
  return stage_->output_count();
}

OutputRef NodeRef::output(size_t index) const {
  FTL_DCHECK(valid());
  FTL_DCHECK(index < stage_->output_count());
  return OutputRef(stage_, index);
}

OutputRef NodeRef::output() const {
  FTL_DCHECK(valid());
  FTL_DCHECK(stage_->output_count() == 1);
  return OutputRef(stage_, 0);
}

bool InputRef::connected() const {
  FTL_DCHECK(valid());
  return actual().connected();
}

const OutputRef& InputRef::mate() const {
  FTL_DCHECK(valid());
  return actual().mate();
}

InputRef::InputRef(Stage* stage, size_t index) : stage_(stage), index_(index) {
  FTL_DCHECK(valid());
}

Input& InputRef::actual() const {
  FTL_DCHECK(valid());
  return stage_->input(index_);
}

bool InputRef::valid() const {
  return stage_ != nullptr && index_ < stage_->input_count();
}

bool OutputRef::connected() const {
  FTL_DCHECK(valid());
  return actual().connected();
}

const InputRef& OutputRef::mate() const {
  FTL_DCHECK(valid());
  return actual().mate();
}

OutputRef::OutputRef(Stage* stage, size_t index)
    : stage_(stage), index_(index) {
  FTL_DCHECK(valid());
}

Output& OutputRef::actual() const {
  FTL_DCHECK(valid());
  return stage_->output(index_);
}

bool OutputRef::valid() const {
  return stage_ != nullptr && index_ < stage_->output_count();
}

}  // namespace media
