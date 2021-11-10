// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_STREAM_IO_TEST_FAKE_BUFFER_PROVIDER_H_
#define SRC_MEDIA_VNEXT_LIB_STREAM_IO_TEST_FAKE_BUFFER_PROVIDER_H_

#include <fuchsia/media2/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/eventpair.h>

namespace fmlib {

// This fake |BufferProvider| doesn't bind (must be called directly) and handles at most one
// collection at a time with only one participant. |BindSysmemToken| is not implemented.
class FakeBufferProvider final : public fuchsia::media2::BufferProvider {
 public:
  FakeBufferProvider() = default;

  ~FakeBufferProvider() override = default;

  // Disallow copy, assign and move.
  FakeBufferProvider(const FakeBufferProvider&) = delete;
  FakeBufferProvider& operator=(const FakeBufferProvider&) = delete;
  FakeBufferProvider(FakeBufferProvider&&) = delete;
  FakeBufferProvider& operator=(FakeBufferProvider&&) = delete;

  // fuchsia::media2::BufferProvider implementation.
  void CreateBufferCollection(zx::eventpair provider_token, std::string vmo_name,
                              CreateBufferCollectionCallback callback) override;

  void GetBuffers(zx::eventpair participant_token, fuchsia::media2::BufferConstraints constraints,
                  fuchsia::media2::BufferRights rights, std::string name, uint64_t id,
                  GetBuffersCallback callback) override;

  void BindSysmemToken(zx::eventpair participant_token, BindSysmemTokenCallback callback) override {
    FX_NOTIMPLEMENTED();
  }

 private:
  void MaybeRespond();

  zx::eventpair provider_token_;
  std::string vmo_name_;
  CreateBufferCollectionCallback create_buffer_collection_callback_;

  zx::eventpair participant_token_;
  fuchsia::media2::BufferConstraints constraints_;
  fuchsia::media2::BufferRights rights_;
  GetBuffersCallback get_buffers_callback_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_STREAM_IO_TEST_FAKE_BUFFER_PROVIDER_H_
