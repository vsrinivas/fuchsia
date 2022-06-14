// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_BUILDER_CREATE_BUFFER_COLLECTION_H_
#define SRC_MEDIA_VNEXT_LIB_BUILDER_CREATE_BUFFER_COLLECTION_H_

#include <fuchsia/media2/cpp/fidl.h>
#include <lib/zx/eventpair.h>

namespace fmlib {

// Uses |buffer_provider| to create a buffer collection and returns two participant tokens for that
// collection.
std::pair<zx::eventpair, zx::eventpair> CreateBufferCollection(
    fuchsia::media2::BufferProvider& buffer_provider);

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_BUILDER_CREATE_BUFFER_COLLECTION_H_
