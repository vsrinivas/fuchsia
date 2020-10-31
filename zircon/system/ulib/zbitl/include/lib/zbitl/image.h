// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZBITL_IMAGE_H_
#define LIB_ZBITL_IMAGE_H_

#include <lib/cksum.h>

#include "checking.h"
#include "view.h"

namespace zbitl {

/// Image provides a modifiable "view" into a ZBI.
template <typename Storage, Checking Check = Checking::kStrict>
class Image : public View<Storage, Check> {
 public:
  using typename View<Storage, Check>::Error;
  using typename View<Storage, Check>::Traits;
  using typename View<Storage, Check>::iterator;
  using typename View<Storage, Check>::header_type;

  static_assert(Traits::CanWrite() && Traits::CanEnsureCapacity(),
                "zbitl::Image requires the ability to write to and extend the underlying storage");

  // Copy/move-constructible or constructible from a Storage argument, like View.
  using View<Storage, Check>::View;

  // Updates the underlying storage to hold an empty ZBI. It is valid to call
  // this method even if the underlying storage does not already represent a
  // ZBI or is too small to do so; it will attempt to extend the capacity and
  // write a new container header.
  fitx::result<Error> clear() {
    if (auto result = Traits::EnsureCapacity(this->storage(), sizeof(zbi_header_t));
        result.is_error()) {
      return fitx::error{
          Error{"cannot increase capacity", sizeof(zbi_header_t), std::move(result.error_value())}};
    }
    if (auto result = this->WriteHeader(ZBI_CONTAINER_HEADER(0), 0); result.is_error()) {
      return fitx::error{
          Error{"cannot write container header", 0, std::move(result.error_value())}};
    }
    return fitx::ok();
  }

  // This version of Append reserves enough space in the underlying ZBI to
  // append an item corresponding to the provided header. The header is
  // sanitized (via `SanitizeHeader`) with the header.length value preserved,
  // as it determines the amount of payload space allocated. The sanitized
  // header is immediately written to the storage and an iterator pointing to
  // the partially written item is returned to the caller on success. It is
  // the caller's responsibility to write the desired data to the payload
  // offset (accessible via the iterator).
  //
  // If `header.flags` has `ZBI_FLAG_CRC32` set, then it is the caller's
  // further responsibility to ensure that `header.crc32` is correct or to use
  // `EditHeader` later on the returned iterator with a correct value.
  fitx::result<Error, iterator> Append(const zbi_header_t& new_header) {
    // Get the size from the container header directly (instead of
    // size_bytes()) to ensure that the underlying storage does indeed
    // represent a ZBI. If we did not check that the following would be able to
    // successfully Append() to a "size 0 ZBI", which is a pathology.
    uint32_t current_length;
    if (auto container_result = this->container_header(); container_result.is_error()) {
      return container_result.take_error();
    } else {
      current_length = container_result.value().length;
    }

    const uint32_t size = sizeof(zbi_header_t) + current_length;
    const uint32_t new_item_offset = size;
    const uint32_t new_size = ZBI_ALIGN(new_item_offset + sizeof(new_header) + new_header.length);
    if (new_size <= size) {
      return fitx::error(Error{"integer overflow; new size is too big", size});
    }

    if (auto result = Traits::EnsureCapacity(this->storage(), new_size); result.is_error()) {
      return fitx::error{Error{"cannot increase capacity", size, std::move(result.error_value())}};
    }

    if (auto result = this->WriteHeader(new_header, new_item_offset); result.is_error()) {
      return fitx::error{
          Error{"cannot write item header", new_item_offset, std::move(result.error_value())}};
    }

    if (auto result =
            this->WriteHeader(ZBI_CONTAINER_HEADER(new_size - uint32_t{sizeof(zbi_header_t)}), 0);
        result.is_error()) {
      return fitx::error{
          Error{"cannot write container header", 0, std::move(result.error_value())}};
    }

    uint32_t padding_size = ZBI_ALIGN(new_header.length) - new_header.length;
    if (padding_size > 0) {
      uint32_t payload_end = new_item_offset + sizeof(new_header) + new_header.length;
      constexpr std::byte kZero[ZBI_ALIGNMENT - 1] = {};
      auto padding = AsBytes(kZero).substr(0, padding_size);
      if (auto result = Traits::Write(this->storage(), payload_end, padding); result.is_error()) {
        return fitx::error{
            Error{"cannot write zero padding", payload_end, std::move(result.error_value())}};
      }
    }

    iterator it;
    it.view_ = this;
    it.offset_ = new_size;

    // `header_type` needs to be constructed from the return value of the
    // Header trait, which might be a reference wrapper to the header in memory
    // instead of the raw value.
    if (auto result = Traits::Header(this->storage(), new_item_offset); result.is_error()) {
      return fitx::error{
          Error{"cannot read header", new_item_offset, std::move(result.error_value())}};
    } else {
      it.header_ = header_type(std::move(result).value());
    }

    if (auto result = Traits::Payload(this->storage(), it.payload_offset(), new_header.length);
        result.is_error()) {
      return fitx::error{Error{"cannot determine payload", it.payload_offset()}};
    } else {
      it.payload_ = result.value();
    }

    return fitx::ok(it);
  }

  // A simpler variation of Append, in which the provided header and payload
  // data are written to underlying storage up front. `header.length` will
  // automatically be set as `data.size()`. Moreover, if the ZBI_FLAG_CRC32
  // flag is provided, the CRC32 will be automatically computed and set as
  // well.
  fitx::result<Error> Append(zbi_header_t header, ByteView data) {
    header.length = static_cast<uint32_t>(data.size());
    if (header.flags & ZBI_FLAG_CRC32) {
      // An item's CRC32 is computed as the hash of its sanitized header with
      // its crc32 field set to 0, combined with the hash of its payload.
      header = SanitizeHeader(header);
      header.crc32 = 0;
      ByteView bytes[] = {AsBytes(header), data};
      uint32_t crc = 0;
      for (ByteView b : bytes) {
        crc = crc32(crc, reinterpret_cast<const uint8_t*>(b.data()), b.size());
      }
      header.crc32 = crc;
    }

    if (auto result = Append(header); result.is_error()) {
      return result.take_error();
    } else if (data.size() > 0) {
      auto it = std::move(result).value();
      ZX_DEBUG_ASSERT(it != this->end());
      uint32_t offset = it.payload_offset();
      if (auto result = Traits::Write(this->storage(), offset, data); result.is_error()) {
        return fitx::error{Error{"cannot write payload", offset, std::move(result.error_value())}};
      }
    }
    return fitx::ok();
  }
};

// Deduction guide: Image img(T{}) instantiates Image<T>.
template <typename Storage>
explicit Image(Storage) -> Image<Storage>;

// A shorthand for permissive checking.
template <typename Storage>
using PermissiveImage = Image<Storage, Checking::kPermissive>;

// A shorthand for CRC checking.
template <typename Storage>
using CrcCheckingImage = Image<Storage, Checking::kCrc>;

}  // namespace zbitl

#endif  // LIB_ZBITL_IMAGE_H_
