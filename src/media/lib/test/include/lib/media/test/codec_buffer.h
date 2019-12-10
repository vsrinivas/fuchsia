// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_TEST_INCLUDE_LIB_MEDIA_TEST_CODEC_BUFFER_H_
#define SRC_MEDIA_LIB_TEST_INCLUDE_LIB_MEDIA_TEST_CODEC_BUFFER_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>
#include <stddef.h>
#include <stdint.h>

#include <memory>

class CodecBuffer {
 public:
  virtual ~CodecBuffer();

  // For now, this always allocates a VMO and pre-maps it into this process's
  // address space.
  //
  // In this example, we're using one buffer per packet mode, so each buffer has
  // a corresponding packet.
  static std::unique_ptr<CodecBuffer> Allocate(
      uint32_t buffer_index, const fuchsia::media::StreamBufferConstraints& constraints);

  static std::unique_ptr<CodecBuffer> CreateFromVmo(uint32_t buffer_index, zx::vmo vmo,
                                                    uint32_t vmo_usable_start,
                                                    uint32_t vmo_usable_size, bool need_write,
                                                    bool is_physically_contiguous);

  // Each successful call to this method dups the VMO handle, with basic rights
  // + read + optional write depending on is_for_write.
  bool GetDupVmo(bool is_for_write, zx::vmo* out_vmo);

  // In buffer-per-packet mode this is equal to the corresponding packet index,
  // for purposes of mapping from packet_index to buffer_index.
  uint32_t buffer_index() const;
  uint8_t* base() const { return base_; }
  size_t size_bytes() const { return size_bytes_; }

  const ::zx::vmo& vmo() const { return vmo_; }
  uint64_t vmo_offset() const { return 0; }

  // For testing.
  bool is_physically_contiguous() const { return is_physically_contiguous_; }

 private:
  explicit CodecBuffer(uint32_t buffer_index, size_t size_bytes);
  void SetPhysicallyContiguousRequired(const ::zx::handle& very_temp_kludge_bti_handle);
  bool AllocateInternal();
  bool CreateFromVmoInternal(zx::vmo vmo, uint32_t vmo_usable_start, uint32_t vmo_usable_size,
                             bool need_write, bool is_physically_contiguous);

  uint32_t buffer_index_ = 0;
  size_t size_bytes_ = 0;

  bool is_physically_contiguous_required_ = false;

  // TODO(dustingreen): Remove this:
  ::zx::bti very_temp_kludge_bti_handle_;

  zx::vmo vmo_;
  uint8_t* base_ = nullptr;
  bool is_physically_contiguous_ = false;
};

#endif  // SRC_MEDIA_LIB_TEST_INCLUDE_LIB_MEDIA_TEST_CODEC_BUFFER_H_
