// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/buffers/cpp/buffer.h"

#include "lib/ftl/logging.h"

namespace mozart {

BufferPtr Duplicate(const Buffer* buffer) {
  if (!buffer)
    return nullptr;

  auto dup = Buffer::New();
  if (buffer->vmo &&
      buffer->vmo.duplicate(MX_RIGHT_SAME_RIGHTS, &dup->vmo) != NO_ERROR)
    return nullptr;
  if (buffer->fence &&
      buffer->fence.duplicate(MX_RIGHT_SAME_RIGHTS, &dup->fence) != NO_ERROR)
    return nullptr;
  if (buffer->retention &&
      buffer->retention.duplicate(MX_RIGHT_SAME_RIGHTS, &dup->retention) !=
          NO_ERROR)
    return nullptr;
  return dup;
}

}  // namespace mozart
