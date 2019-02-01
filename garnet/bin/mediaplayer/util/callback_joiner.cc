// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/util/callback_joiner.h"

namespace media_player {

// static
std::shared_ptr<CallbackJoiner> CallbackJoiner::Create() {
  return std::make_shared<CallbackJoiner>();
}

CallbackJoiner::CallbackJoiner() {}

CallbackJoiner::~CallbackJoiner() {}

void CallbackJoiner::Complete() {
  FXL_DCHECK(counter_ != 0);
  --counter_;
  if (counter_ == 0 && join_callback_) {
    fit::closure join_callback = std::move(join_callback_);
    join_callback();
  }
}

fit::closure CallbackJoiner::NewCallback() {
  Spawn();
  std::shared_ptr<CallbackJoiner> this_ptr = shared_from_this();
  FXL_DCHECK(!this_ptr.unique());
  return [this_ptr]() {
    FXL_DCHECK(this_ptr);
    this_ptr->Complete();
  };
}

void CallbackJoiner::WhenJoined(fit::closure join_callback) {
  FXL_DCHECK(join_callback);
  FXL_DCHECK(!join_callback_);
  if (counter_ == 0) {
    join_callback();
  } else {
    join_callback_ = std::move(join_callback);
  }
}

bool CallbackJoiner::Cancel() {
  bool result = !!join_callback_;
  join_callback_ = nullptr;
  return result;
}

}  // namespace media_player
