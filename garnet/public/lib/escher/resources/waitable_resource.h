// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_RESOURCES_WAITABLE_RESOURCE_H_
#define LIB_ESCHER_RESOURCES_WAITABLE_RESOURCE_H_

#include "lib/escher/renderer/semaphore.h"
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

  // Clients should be careful with this, since it could cause missed
  // dependencies. Should be safe to call for a repeated operation.
  void ReplaceWaitSemaphore(SemaphorePtr semaphore);

  SemaphorePtr TakeWaitSemaphore() { return std::move(wait_semaphore_); }
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
  // specific usage-pattern that first triggers it; we'll deal with this
  // situation when it first arises.
  FXL_CHECK(!wait_semaphore_);
  wait_semaphore_ = std::move(semaphore);
}

inline void WaitableResource::ReplaceWaitSemaphore(SemaphorePtr semaphore) {
  wait_semaphore_ = std::move(semaphore);
}

}  // namespace escher

#endif  // LIB_ESCHER_RESOURCES_WAITABLE_RESOURCE_H_
