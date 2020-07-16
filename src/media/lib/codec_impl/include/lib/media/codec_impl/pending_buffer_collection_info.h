// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_PENDING_BUFFER_COLLECTION_INFO_H_
#define SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_PENDING_BUFFER_COLLECTION_INFO_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/media/codec_impl/codec_port.h>

#include <optional>

namespace codec_impl {
namespace internal {

// This is an async context class for use while CodecImpl is waiting for a BufferCollection to be
// allocated and potentially an auxiliary BufferCollection. It is move-only.
class PendingBufferCollectionInfo {
 public:
  using InfoResult = fit::result<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t>;

  PendingBufferCollectionInfo(
      CodecPort port, uint64_t buffer_lifetime_ordinal,
      const std::optional<fuchsia::sysmem::BufferCollectionConstraintsAuxBuffers>&
          aux_buffer_constraints = std::nullopt);
  PendingBufferCollectionInfo() = delete;

  PendingBufferCollectionInfo(PendingBufferCollectionInfo&&) = default;
  PendingBufferCollectionInfo& operator=(PendingBufferCollectionInfo&&) = default;

  PendingBufferCollectionInfo(const PendingBufferCollectionInfo&) = delete;
  PendingBufferCollectionInfo& operator=(const PendingBufferCollectionInfo&) = delete;

  // Accessors
  CodecPort port() const { return port_; }
  uint64_t buffer_lifetime_ordinal() const { return buffer_lifetime_ordinal_; }
  const InfoResult& buffer_collection() const { return buffer_collection_; }
  const InfoResult& aux_buffer_collection() const { return aux_buffer_collection_; }

  // Mutators
  void set_buffer_collection_info(zx_status_t status, fuchsia::sysmem::BufferCollectionInfo_2 info);
  void set_aux_buffer_collection_info(zx_status_t status,
                                      fuchsia::sysmem::BufferCollectionInfo_2 info);
  fuchsia::sysmem::BufferCollectionInfo_2 TakeBufferCollectionInfo();
  std::optional<fuchsia::sysmem::BufferCollectionInfo_2> TakeAuxBufferCollectionInfo();

  // Observers
  bool AllowsAuxBuffersForSecure() const {
    return aux_buffer_requirement_ != AuxBufferRequirement::DISALLOWED;
  }
  bool NeedsAuxBuffersForSecure() const {
    return aux_buffer_requirement_ == AuxBufferRequirement::REQUIRED;
  }

  bool HasError() const {
    return buffer_collection_.is_error() || aux_buffer_collection_.is_error();
  }
  bool IsReady() const {
    return buffer_collection_ && (!AllowsAuxBuffersForSecure() || aux_buffer_collection_);
  }

  bool HasValidAuxBufferCollection() const;

 private:
  enum class AuxBufferRequirement { DISALLOWED, ALLOWED, REQUIRED };

  static AuxBufferRequirement GetAuxBufferRequirement(
      const std::optional<fuchsia::sysmem::BufferCollectionConstraintsAuxBuffers>&
          aux_buffer_constraints);

  CodecPort port_;
  uint64_t buffer_lifetime_ordinal_;
  AuxBufferRequirement aux_buffer_requirement_;

  InfoResult buffer_collection_;
  InfoResult aux_buffer_collection_;
};

}  // namespace internal
}  // namespace codec_impl

#endif  // SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_PENDING_BUFFER_COLLECTION_INFO_H_
