// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>

#include "garnet/bin/mediaplayer/util/incident.h"

Incident::Incident(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

Incident::~Incident() {}

void Incident::Occur() {
  if (occurred_) {
    return;
  }

  occurred_ = true;

  // Swap out consequences_ in case one of the callbacks deletes this.
  std::vector<fit::closure> consequences;
  consequences_.swap(consequences);

  for (auto& consequence : consequences) {
    InvokeConsequence(std::move(consequence));
  }
}

void Incident::InvokeConsequence(fit::closure consequence) {
  if (!dispatcher_) {
    consequence();
    return;
  }

  async::PostTask(dispatcher_, std::move(consequence));
}

ThreadsafeIncident::ThreadsafeIncident() {}

ThreadsafeIncident::~ThreadsafeIncident() {}

void ThreadsafeIncident::Occur() {
  std::vector<fit::closure> consequences;

  {
    std::lock_guard<std::mutex> locker(mutex_);

    if (occurred_) {
      return;
    }

    occurred_ = true;
    consequences_.swap(consequences);
  }

  for (auto& consequence : consequences) {
    consequence();
  }
}
