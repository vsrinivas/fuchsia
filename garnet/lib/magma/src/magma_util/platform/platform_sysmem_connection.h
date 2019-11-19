// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_SYSMEM_CONNECTION_H_
#define GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_SYSMEM_CONNECTION_H_

#include <memory>

#include "magma.h"
#include "magma_sysmem.h"
#include "magma_util/status.h"
#include "platform_buffer.h"

namespace magma_sysmem {
class PlatformBufferDescription {
 public:
  virtual ~PlatformBufferDescription() = default;
  virtual bool is_secure() const = 0;
  virtual uint32_t count() const = 0;
  virtual bool has_format_modifier() const = 0;
  virtual uint64_t format_modifier() const = 0;
  virtual uint32_t coherency_domain() const = 0;
  virtual bool GetPlanes(uint64_t width, uint64_t height,
                         magma_image_plane_t* planes_out) const = 0;
};

class PlatformBufferConstraints {
 public:
  virtual ~PlatformBufferConstraints() {}

  virtual magma::Status SetImageFormatConstraints(
      uint32_t index, const magma_image_format_constraints_t* format_constraints) = 0;
};

class PlatformBufferCollection {
 public:
  virtual ~PlatformBufferCollection() {}

  virtual magma::Status SetConstraints(PlatformBufferConstraints* constraints) = 0;
  virtual magma::Status GetBufferDescription(
      std::unique_ptr<PlatformBufferDescription>* description_out) = 0;
  virtual magma::Status GetBufferHandle(uint32_t index, uint32_t* handle_out,
                                        uint32_t* offset_out) = 0;
};

class PlatformSysmemConnection {
 public:
  virtual ~PlatformSysmemConnection() {}

  static std::unique_ptr<PlatformSysmemConnection> Import(uint32_t handle);

  static magma_status_t DecodeBufferDescription(
      const uint8_t* image_data, uint64_t image_data_size,
      std::unique_ptr<PlatformBufferDescription>* buffer_description_out);

  virtual magma_status_t AllocateBuffer(uint32_t flags, size_t size,
                                        std::unique_ptr<magma::PlatformBuffer>* buffer_out) = 0;

  virtual magma::Status CreateBufferCollectionToken(uint32_t* handle_out) = 0;
  virtual magma::Status ImportBufferCollection(
      uint32_t handle, std::unique_ptr<PlatformBufferCollection>* collection_out) = 0;

  virtual magma::Status CreateBufferConstraints(
      const magma_buffer_format_constraints_t* constraints,
      std::unique_ptr<PlatformBufferConstraints>* constraints_out) = 0;
};

}  // namespace magma_sysmem

#endif  // GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_SYSMEM_CONNECTION_H_
