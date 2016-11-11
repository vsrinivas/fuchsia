// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SERVICES_BUFFERS_CPP_BUFFER_H_
#define APPS_MOZART_SERVICES_BUFFERS_CPP_BUFFER_H_

#include "apps/mozart/services/buffers/buffer.fidl.h"

namespace mozart {

// Produces a duplicate of a buffer which references the same content.
// Returns nullptr if |buffer| is null or if it could not be duplicated.
BufferPtr Duplicate(const Buffer* buffer);

}  // namespace mozart

#endif  // APPS_MOZART_SERVICES_BUFFERS_CPP_BUFFER_H_
