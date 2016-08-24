// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/common/incident.h"

namespace mojo {
namespace media {

Incident::Incident() {}

Incident::~Incident() {}

void Incident::Occur() {
  if (occurred_) {
    return;
  }

  occurred_ = true;

  // Swap out consequences_ in case one of the callbacks deletes this.
  std::vector<std::function<void()>> consequences;
  consequences_.swap(consequences);

  for (const std::function<void()>& consequence : consequences) {
    consequence();
  }
}

ThreadsafeIncident::ThreadsafeIncident() {}

ThreadsafeIncident::~ThreadsafeIncident() {}

void ThreadsafeIncident::Occur() {
  std::vector<std::function<void()>> consequences;

  {
    base::AutoLock lock(consequences_lock_);

    if (occurred_) {
      return;
    }

    occurred_ = true;
    consequences_.swap(consequences);
  }

  for (const std::function<void()>& consequence : consequences) {
    consequence();
  }
}

}  // namespace media
}  // namespace mojo
