// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/framework/refs.h"

#include "apps/media/services/framework/stages/input.h"
#include "apps/media/services/framework/stages/output.h"
#include "apps/media/services/framework/stages/stage.h"
#include "lib/ftl/logging.h"

namespace mojo {
namespace media {

size_t PartRef::input_count() const {
  DCHECK(valid());
  return stage_->input_count();
}

InputRef PartRef::input(size_t index) const {
  DCHECK(valid());
  DCHECK(index < stage_->input_count());
  return InputRef(stage_, index);
}

InputRef PartRef::input() const {
  DCHECK(valid());
  DCHECK(stage_->input_count() == 1);
  return InputRef(stage_, 0);
}

size_t PartRef::output_count() const {
  DCHECK(valid());
  return stage_->output_count();
}

OutputRef PartRef::output(size_t index) const {
  DCHECK(valid());
  DCHECK(index < stage_->output_count());
  return OutputRef(stage_, index);
}

OutputRef PartRef::output() const {
  DCHECK(valid());
  DCHECK(stage_->output_count() == 1);
  return OutputRef(stage_, 0);
}

bool InputRef::connected() const {
  DCHECK(valid());
  return actual().connected();
}

const OutputRef& InputRef::mate() const {
  DCHECK(valid());
  return actual().mate();
}

InputRef::InputRef(Stage* stage, size_t index) : stage_(stage), index_(index) {
  DCHECK(valid());
}

Input& InputRef::actual() const {
  DCHECK(valid());
  return stage_->input(index_);
}

bool InputRef::valid() const {
  return stage_ != nullptr && index_ < stage_->input_count();
}

bool OutputRef::connected() const {
  DCHECK(valid());
  return actual().connected();
}

const InputRef& OutputRef::mate() const {
  DCHECK(valid());
  return actual().mate();
}

OutputRef::OutputRef(Stage* stage, size_t index)
    : stage_(stage), index_(index) {
  DCHECK(valid());
}

Output& OutputRef::actual() const {
  DCHECK(valid());
  return stage_->output(index_);
}

bool OutputRef::valid() const {
  return stage_ != nullptr && index_ < stage_->output_count();
}

}  // namespace media
}  // namespace mojo
