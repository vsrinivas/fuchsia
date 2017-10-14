// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/renderer/semaphore_wait.h"
#include "lib/escher/resources/resource.h"

namespace escher {

// WaitableResource is a base class for resources that can have an associated
// "wait semaphore"; if non-null, it will be applied to the next CommandBuffer
// submission.
class WaitableResource : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  // TODO: eventually make these private, callable only by friends.
  void SetWaitSemaphore(SemaphorePtr semaphore);
  SemaphorePtr TakeWaitSemaphore();

  bool HasWaitSemaphore() const { return !!wait_semaphore_; }

 protected:
  explicit WaitableResource(ResourceManager* owner);

 private:
  SemaphorePtr wait_semaphore_;

  FXL_DISALLOW_COPY_AND_ASSIGN(WaitableResource);
};

// Inline function definitions.

inline void WaitableResource::SetWaitSemaphore(SemaphorePtr semaphore) {
  // This is not necessarily an error, but the consequences will depend on the
  // specific usage-pattern that first triggers it; we'll deal with it then.
  FXL_CHECK(!wait_semaphore_);
  wait_semaphore_ = std::move(semaphore);
}

inline SemaphorePtr WaitableResource::TakeWaitSemaphore() {
  return std::move(wait_semaphore_);
}

}  // namespace escher
