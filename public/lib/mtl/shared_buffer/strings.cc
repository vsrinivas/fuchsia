// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>
#include <mojo/system/buffer.h>
#include <vector>

#include "lib/mtl/shared_buffer/strings.h"

#include "lib/ftl/logging.h"

namespace mtl {

bool SharedBufferFromString(const ftl::StringView& string,
                            mojo::ScopedSharedBufferHandle* handle_ptr) {
  FTL_DCHECK(handle_ptr);

  mojo::ScopedSharedBufferHandle buffer_handle;
  MojoResult result =
      mojo::CreateSharedBuffer(nullptr, string.size(), &buffer_handle);
  if (result != MOJO_RESULT_OK) {
    FTL_LOG(WARNING) << "mojo::CreateSharedBuffer failed: " << result;
    return false;
  }

  if (!string.empty()) {
    mx_handle_t vmo_handle = (mx_handle_t)buffer_handle.get().value();
    ssize_t r = mx_vmo_write(vmo_handle, string.data(), 0, string.size());
    if (r < 0) {
      FTL_LOG(WARNING) << "mx_vmo_write failed: " << r;
      return false;
    }
  }

  *handle_ptr = std::move(buffer_handle);

  return true;
}

bool StringFromSharedBuffer(const mojo::ScopedSharedBufferHandle& shared_buffer,
                            std::string* string_ptr) {
  FTL_DCHECK(string_ptr);

  MojoBufferInformation info;
  MojoResult result = MojoGetBufferInformation(shared_buffer.get().value(),
                                               &info, sizeof(info));
  if (result != MOJO_RESULT_OK) {
    FTL_LOG(WARNING) << "MojoGetBufferInformation failed: " << result;
    return false;
  }

  if (info.num_bytes == 0) {
    // Nothing to read, just truncate the supplied string.
    string_ptr->resize(0);
    return true;
  }

  std::string string;
  string.resize(info.num_bytes);
  char* string_chars = &string[0];

  mx_handle_t vmo_handle = (mx_handle_t)shared_buffer.get().value();
  ssize_t r = mx_vmo_read(vmo_handle, string_chars, 0, info.num_bytes);
  if (r < 0) {
    FTL_LOG(WARNING) << "mx_vmo_read failed: " << r;
    return false;
  }

  *string_ptr = std::move(string);
  return true;
}

}  // namespace mtl
