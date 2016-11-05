// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>
#include <vector>

#include "lib/mtl/vmo/strings.h"

#include "lib/ftl/logging.h"

namespace mtl {

namespace {

template <typename Container>
bool VmoFromContainer(const Container& container, mx::vmo* handle_ptr) {
  FTL_CHECK(handle_ptr);

  uint64_t num_bytes = container.size();
  mx_status_t status = mx::vmo::create(num_bytes, 0u, handle_ptr);
  if (status < 0) {
    FTL_LOG(WARNING) << "mx::vmo::create failed: " << status;
    return false;
  }

  if (num_bytes == 0) {
    return true;
  }

  mx_size_t actual;
  status = handle_ptr->write(container.data(), 0, num_bytes, &actual);
  if (status < 0) {
    FTL_LOG(WARNING) << "mx::vmo::write failed: " << status;
    return false;
  }
  if ((size_t)actual != num_bytes) {
    FTL_LOG(WARNING) << "mx::vmo::write wrote " << actual << " bytes instead of "
                     << num_bytes << " bytes.";
    return false;
  }

  return true;
}

template <typename Container>
bool ContainerFromVmo(const mx::vmo& buffer, Container* container_ptr) {
  FTL_CHECK(container_ptr);

  uint64_t num_bytes;
  mx_status_t status = buffer.get_size(&num_bytes);
  if (status != NO_ERROR) {
    FTL_LOG(WARNING) << "mx::vmo::get_size failed: " << status;
    return false;
  }

  container_ptr->resize(num_bytes);

  if (num_bytes == 0) {
    return true;
  }

  mx_size_t num_read;
  status = buffer.read(&(*container_ptr)[0], 0, num_bytes, &num_read);
  if (status < 0) {
    FTL_LOG(WARNING) << "mx::vmo::read failed: " << status;
    return false;
  }

  if ((size_t)num_read != num_bytes) {
    FTL_LOG(WARNING) << "mx::vmo::write wrote " << num_read
                     << " bytes instead of " << num_bytes << " bytes.";
    return false;
  }

  return true;
}

}  // namespace

bool VmoFromString(const ftl::StringView& string, mx::vmo* handle_ptr) {
  return VmoFromContainer<ftl::StringView>(string, handle_ptr);
}

bool StringFromVmo(const mx::vmo& shared_buffer,
                   std::string* string_ptr) {
  return ContainerFromVmo<std::string>(shared_buffer, string_ptr);
}

bool VmoFromVector(const std::vector<char>& vector, mx::vmo* handle_ptr) {
  return VmoFromContainer<std::vector<char>>(vector, handle_ptr);
}

bool VectorFromVmo(const mx::vmo& shared_buffer,
                   std::vector<char>* vector_ptr) {
  return ContainerFromVmo<std::vector<char>>(shared_buffer,
                                                      vector_ptr);
}

bool VmoFromVector(const std::vector<uint8_t>& vector,
                   mx::vmo* handle_ptr) {
  return VmoFromContainer<std::vector<uint8_t>>(vector, handle_ptr);
}

bool VectorFromVmo(const mx::vmo& shared_buffer,
                   std::vector<uint8_t>* vector_ptr) {
  return ContainerFromVmo<std::vector<uint8_t>>(shared_buffer, vector_ptr);
}

}  // namespace mtl
