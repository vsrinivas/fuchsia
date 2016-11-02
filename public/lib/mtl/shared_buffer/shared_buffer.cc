// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>
#include <mojo/system/buffer.h>
#include <vector>

#include "lib/mtl/shared_buffer/strings.h"

#include "lib/ftl/logging.h"

namespace mtl {

namespace {

template <typename Container>
bool SharedBufferFromContainer(const Container& container,
                               mojo::ScopedSharedBufferHandle* handle_ptr) {
  FTL_DCHECK(handle_ptr);

  uint64_t num_bytes = container.size();
  mx_handle_t mx_handle = MX_HANDLE_INVALID;
  mx_status_t status = mx_vmo_create(num_bytes, 0u, &mx_handle);
  if (status < 0) {
    FTL_LOG(WARNING) << "mx_vmo_create failed: " << status;
    return false;
  }

  mojo::SharedBufferHandle mojo_handle(mx_handle);
  handle_ptr->reset(mojo_handle);

  if (num_bytes == 0) {
    return true;
  }

  mx_size_t actual;
  status = mx_vmo_write(mx_handle, container.data(), 0, num_bytes, &actual);
  if (status < 0) {
    FTL_LOG(WARNING) << "mx_vmo_write failed: " << status;
    return false;
  }
  if ((size_t)actual != num_bytes) {
    FTL_LOG(WARNING) << "mx_vmo_write wrote " << actual << " bytes instead of "
                     << num_bytes << " bytes.";
    return false;
  }

  return true;
}

template <typename Container>
bool ContainerFromSharedBuffer(
    const mojo::ScopedSharedBufferHandle& shared_buffer,
    Container* container_ptr) {
  FTL_CHECK(container_ptr);

  mx_handle_t vmo_handle = (mx_handle_t)shared_buffer.get().value();

  uint64_t num_bytes;
  mx_status_t status = mx_vmo_get_size(vmo_handle, &num_bytes);
  if (status != NO_ERROR) {
    FTL_LOG(WARNING) << "mx_vmo_get_size failed: " << status;
    return false;
  }

  container_ptr->resize(num_bytes);

  if (num_bytes == 0) {
    return true;
  }

  mx_size_t num_read;
  mx_status_t status = mx_vmo_read(vmo_handle, &(*container_ptr)[0], 0, num_bytes, &num_read);
  if (status < 0) {
    FTL_LOG(WARNING) << "mx_vmo_read failed: " << status;
    return false;
  }

  if ((size_t)num_read != num_bytes) {
    FTL_LOG(WARNING) << "mx_vmo_write wrote " << num_read
                     << " bytes instead of " << num_bytes << " bytes.";
    return false;
  }

  return true;
}

}  // namespace

bool SharedBufferFromString(const ftl::StringView& string,
                            mojo::ScopedSharedBufferHandle* handle_ptr) {
  return SharedBufferFromContainer<ftl::StringView>(string, handle_ptr);
}

bool StringFromSharedBuffer(const mojo::ScopedSharedBufferHandle& shared_buffer,
                            std::string* string_ptr) {
  return ContainerFromSharedBuffer<std::string>(shared_buffer, string_ptr);
}

bool SharedBufferFromVector(const std::vector<char>& vector,
                            mojo::ScopedSharedBufferHandle* handle_ptr) {
  return SharedBufferFromContainer<std::vector<char>>(vector, handle_ptr);
}

bool VectorFromSharedBuffer(const mojo::ScopedSharedBufferHandle& shared_buffer,
                            std::vector<char>* vector_ptr) {
  return ContainerFromSharedBuffer<std::vector<char>>(shared_buffer,
                                                      vector_ptr);
}

bool SharedBufferFromVector(const std::vector<uint8_t>& vector,
                            mojo::ScopedSharedBufferHandle* handle_ptr) {
  return SharedBufferFromContainer<std::vector<uint8_t>>(vector, handle_ptr);
}

bool VectorFromSharedBuffer(const mojo::ScopedSharedBufferHandle& shared_buffer,
                            std::vector<uint8_t>* vector_ptr) {
  return ContainerFromSharedBuffer<std::vector<uint8_t>>(shared_buffer,
                                                         vector_ptr);
}

}  // namespace mtl
