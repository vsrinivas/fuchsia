// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <vector>

#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/vmo/strings.h"
#include "src/ledger/lib/vmo/vector.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

namespace {

template <typename Container>
bool VmoFromContainer(const Container& container, SizedVmo* sized_vmo_ptr) {
  LEDGER_CHECK(sized_vmo_ptr);

  uint64_t num_bytes = container.size();
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(num_bytes, 0u, &vmo);
  if (status < 0) {
    LEDGER_LOG(WARNING) << "zx::vmo::create failed: " << zx_status_get_string(status);
    return false;
  }

  if (num_bytes > 0) {
    status = vmo.write(container.data(), 0, num_bytes);
    if (status < 0) {
      LEDGER_LOG(WARNING) << "zx::vmo::write failed: " << zx_status_get_string(status);
      return false;
    }
  }

  *sized_vmo_ptr = SizedVmo(std::move(vmo), num_bytes);

  return true;
}

template <typename Container>
bool ContainerFromVmo(const zx::vmo& buffer, uint64_t num_bytes, Container* container_ptr) {
  LEDGER_CHECK(container_ptr);

  container_ptr->resize(num_bytes);

  if (num_bytes == 0) {
    return true;
  }

  zx_status_t status = buffer.read(&(*container_ptr)[0], 0, num_bytes);
  if (status < 0) {
    LEDGER_LOG(WARNING) << "zx::vmo::read failed: " << zx_status_get_string(status);
    return false;
  }

  return true;
}

}  // namespace

bool VmoFromString(const absl::string_view& string, SizedVmo* sized_vmo) {
  return VmoFromContainer<absl::string_view>(string, sized_vmo);
}

bool VmoFromString(const absl::string_view& string, fuchsia::mem::Buffer* buffer_ptr) {
  ledger::SizedVmo sized_vmo;
  if (!VmoFromContainer<absl::string_view>(string, &sized_vmo)) {
    return false;
  }
  *buffer_ptr = std::move(sized_vmo).ToTransport();
  return true;
}

bool StringFromVmo(const SizedVmo& shared_buffer, std::string* string_ptr) {
  return ContainerFromVmo<std::string>(shared_buffer.vmo(), shared_buffer.size(), string_ptr);
}

bool StringFromVmo(const fuchsia::mem::Buffer& vmo_transport, std::string* string_ptr) {
  if (!SizedVmo::IsSizeValid(vmo_transport.vmo, vmo_transport.size)) {
    return false;
  }
  return ContainerFromVmo<std::string>(vmo_transport.vmo, vmo_transport.size, string_ptr);
}

bool StringFromVmo(const fuchsia::mem::Buffer& vmo_transport, size_t num_bytes,
                   std::string* string_ptr) {
  if (!SizedVmo::IsSizeValid(vmo_transport.vmo, num_bytes)) {
    return false;
  }
  return ContainerFromVmo<std::string>(vmo_transport.vmo, num_bytes, string_ptr);
}

bool VmoFromVector(const std::vector<char>& vector, SizedVmo* sized_vmo) {
  return VmoFromContainer<std::vector<char>>(vector, sized_vmo);
}

bool VmoFromVector(const std::vector<char>& vector, fuchsia::mem::Buffer* buffer_ptr) {
  ledger::SizedVmo sized_vmo;
  if (!VmoFromContainer<std::vector<char>>(vector, &sized_vmo)) {
    return false;
  }
  *buffer_ptr = std::move(sized_vmo).ToTransport();
  return true;
}

bool VectorFromVmo(const SizedVmo& shared_buffer, std::vector<char>* vector_ptr) {
  return ContainerFromVmo<std::vector<char>>(shared_buffer.vmo(), shared_buffer.size(), vector_ptr);
}

bool VectorFromVmo(const fuchsia::mem::Buffer& vmo_transport, std::vector<char>* vector_ptr) {
  if (!SizedVmo::IsSizeValid(vmo_transport.vmo, vmo_transport.size)) {
    return false;
  }
  return ContainerFromVmo<std::vector<char>>(vmo_transport.vmo, vmo_transport.size, vector_ptr);
}

bool VmoFromVector(const std::vector<uint8_t>& vector, SizedVmo* sized_vmo) {
  return VmoFromContainer<std::vector<uint8_t>>(vector, sized_vmo);
}

bool VmoFromVector(const std::vector<uint8_t>& vector, fuchsia::mem::Buffer* buffer_ptr) {
  ledger::SizedVmo sized_vmo;
  if (!VmoFromContainer<std::vector<uint8_t>>(vector, &sized_vmo)) {
    return false;
  }
  *buffer_ptr = std::move(sized_vmo).ToTransport();
  return true;
}

bool VectorFromVmo(const SizedVmo& shared_buffer, std::vector<uint8_t>* vector_ptr) {
  return ContainerFromVmo<std::vector<uint8_t>>(shared_buffer.vmo(), shared_buffer.size(),
                                                vector_ptr);
}

bool VectorFromVmo(const fuchsia::mem::Buffer& vmo_transport, std::vector<uint8_t>* vector_ptr) {
  if (!SizedVmo::IsSizeValid(vmo_transport.vmo, vmo_transport.size)) {
    return false;
  }
  return ContainerFromVmo<std::vector<uint8_t>>(vmo_transport.vmo, vmo_transport.size, vector_ptr);
}

}  // namespace ledger
