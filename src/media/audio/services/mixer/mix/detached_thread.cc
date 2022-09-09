// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/detached_thread.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/services/common/logging.h"

namespace media_audio {

DetachedThreadPtr DetachedThread::Create() {
  // std::make_shared requires a public ctor, but we hide our ctor to force callers to use Create.
  struct WithPublicCtor : public DetachedThread {
    WithPublicCtor() = default;
  };

  return std::make_shared<WithPublicCtor>();
}

}  // namespace media_audio
