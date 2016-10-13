// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/renderer/semaphore_wait.h"
#include "ftl/memory/ref_counted.h"

namespace escher {
namespace impl {

class EscherImpl;
class RenderFrame;

// A Resource is a ref-counted object that is kept alive by the Escher
// implementation as long as necessary (i.e. until all GPU submissions that
// use the resource have finished), even if it is no longer referenced by an
// Escher client.  In addition, Resources support automatic tracking of
// inter-submission dependencies.
class Resource : public ftl::RefCountedThreadSafe<Resource> {
 public:
  // TODO: eventually make these private, callable only by friends.
  void SetWaitSemaphore(SemaphorePtr semaphore);
  SemaphorePtr TakeWaitSemaphore();

 protected:
  // It is allowable to pass nullptr to signify that the Resource is not owned
  // by Escher (i.e. it wraps a client-provided image/buffer/etc. and the client
  // is responsible for eventually destroying it).
  Resource(EscherImpl* escher);

  FRIEND_REF_COUNTED_THREAD_SAFE(Resource);
  virtual ~Resource();

  EscherImpl* escher() const { return escher_; }

 private:
  SemaphorePtr wait_semaphore_;

  // TODO: consider removing this variable... it's not clear that we need it.
  EscherImpl* const escher_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Resource);
};

typedef ftl::RefPtr<Resource> ResourcePtr;

// Inline function definitions.

inline void Resource::SetWaitSemaphore(SemaphorePtr semaphore) {
  // This is not necessarily an error, but the consequences will depend on the
  // specific usage-pattern that first triggers it; we'll deal with it then.
  FTL_CHECK(!wait_semaphore_);
  wait_semaphore_ = semaphore;
}

inline SemaphorePtr Resource::TakeWaitSemaphore() {
  return std::move(wait_semaphore_);
}

}  // namespace impl
}  // namespace escher
