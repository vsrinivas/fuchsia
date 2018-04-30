// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/function.h>
#include <lib/async/dispatcher.h>

namespace btlib {
namespace common {

// Runs |task|. Posts it on |dispatcher| if dispatcher is not null.
void RunOrPost(fbl::Closure task, async_t* dispatcher);

}  // namespace common
}  // namespace btlib
