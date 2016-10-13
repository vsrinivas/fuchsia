// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_SHARED_BUFFER_STRINGS_H_
#define LIB_MTL_SHARED_BUFFER_STRINGS_H_

#include <string>

#include "lib/ftl/strings/string_view.h"
#include "mojo/public/cpp/system/buffer.h"

namespace mtl {

// Make a new shared buffer with the contents of a string.
bool SharedBufferFromString(const ftl::StringView& string,
                            mojo::ScopedSharedBufferHandle* handle_ptr);

// Copy the contents of a shared buffer into a string.
bool StringFromSharedBuffer(const mojo::ScopedSharedBufferHandle& shared_buffer,
                            std::string* string_ptr);

}  // namespace mtl

#endif  // LIB_MTL_SHARED_BUFFER_STRINGS_H_
