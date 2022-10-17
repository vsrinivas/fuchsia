// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_H_
#define SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_H_

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/fidl/cpp/wire/message.h>

namespace ddk {

// Gets the raw metadata.
// Returns an error if metadata does not exist.
inline zx::result<std::vector<uint8_t>> GetMetadataBlob(zx_device_t* dev, uint32_t type) {
  size_t metadata_size;
  zx_status_t status = device_get_metadata_size(dev, type, &metadata_size);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  std::vector<uint8_t> ret(metadata_size);
  size_t actual;
  status = device_get_metadata(dev, type, ret.data(), metadata_size, &actual);
  if (status != ZX_OK || actual != metadata_size) {
    return zx::error(status);
  }
  return zx::ok(std::move(ret));
}

// Gets a metadata that is contained in a specific struct.
// Checks that the size of the metadata corresponds to the struct size.
template <class T>
zx::result<std::unique_ptr<T>> GetMetadata(zx_device_t* dev, uint32_t type) {
  auto metadata = GetMetadataBlob(dev, type);
  if (!metadata.is_ok()) {
    return metadata.take_error();
  }
  if (metadata->size() != sizeof(T)) {
    zxlogf(ERROR, "Metadata size retrieved [%lu] does not match size of metadata struct [%lu]",
           metadata->size(), sizeof(T));
    return zx::error(ZX_ERR_INTERNAL);
  }
  return zx::ok(std::make_unique<T>(*reinterpret_cast<T*>(metadata->data())));
}

// Gets a metadata that is contained in an array of struct T.
// Checks that the size of the metadata corresponds to the struct size.
template <class T>
zx::result<std::vector<T>> GetMetadataArray(zx_device_t* dev, uint32_t type) {
  auto metadata = GetMetadataBlob(dev, type);
  if (!metadata.is_ok()) {
    return metadata.take_error();
  }
  if ((metadata->size() % sizeof(T)) != 0) {
    zxlogf(ERROR,
           "Metadata size retrieved [%lu] was not an integer multiple of metadata struct [%lu]",
           metadata->size(), sizeof(T));
    return zx::error(ZX_ERR_INTERNAL);
  }
  auto mstart = reinterpret_cast<T*>(metadata->data());
  return zx::ok(std::vector<T>(mstart, mstart + (metadata->size() / sizeof(T))));
}

template <typename T>
class DecodedMetadata {
 public:
  DecodedMetadata(std::vector<uint8_t> metadata_blob) {
    metadata_blob_ = metadata_blob;
    decoded_ = std::make_unique<fidl::unstable::DecodedMessage<T>>(
        fidl::internal::WireFormatVersion::kV2, metadata_blob_.data(), metadata_blob_.size());
  }

  T* PrimaryObject() { return decoded_->PrimaryObject(); }
  bool ok() { return decoded_->ok(); }

 private:
  std::vector<uint8_t> metadata_blob_;
  std::unique_ptr<fidl::unstable::DecodedMessage<T>> decoded_;
};

// Gets metadata that is enoded in a specific fidl wire format.
// Decodes the metadata and returns a DecodedMetadata object, which stores the raw
// data as well as the decoded struct.
template <typename T>
zx::result<DecodedMetadata<T>> GetEncodedMetadata(zx_device_t* dev, uint32_t type) {
  auto metadata = GetMetadataBlob(dev, type);
  if (!metadata.is_ok()) {
    return metadata.take_error();
  }
  DecodedMetadata<T> decoded(metadata.value());
  if (!decoded.ok()) {
    zxlogf(ERROR, "Failed to deserialize metadata.");
    return zx::error(ZX_ERR_INTERNAL);
  }
  return zx::make_result(ZX_OK, std::move(decoded));
}

}  // namespace ddk

#endif  // SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_H_
