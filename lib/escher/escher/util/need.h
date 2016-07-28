// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "ftl/memory/ref_counted.h"

namespace escher {

// Needs are used to notify managers that a managed object is no longer in use.
// This is accomplished by overriding the destructor.
class Need : public ftl::RefCountedThreadSafe<Need> {
 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(Need);
  Need() {}
  virtual ~Need() {}

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Need);
};

}  // namespace escher
