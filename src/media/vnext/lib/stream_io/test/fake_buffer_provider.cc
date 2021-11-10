// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/stream_io/test/fake_buffer_provider.h"

#include <lib/fpromise/bridge.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>

namespace fmlib {
namespace {

// Gets the koid for a handle.
template <typename T>
zx_koid_t GetKoid(const T& handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return ZX_KOID_INVALID;
  }

  return info.koid;
}

// Gets the peer koid for a handle.
template <typename T>
zx_koid_t GetPeerKoid(const T& handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return ZX_KOID_INVALID;
  }

  return info.related_koid;
}

}  // namespace

void FakeBufferProvider::CreateBufferCollection(zx::eventpair provider_token, std::string vmo_name,
                                                CreateBufferCollectionCallback callback) {
  FX_CHECK(provider_token);
  FX_CHECK(callback);

  FX_CHECK(!provider_token_);

  provider_token_ = std::move(provider_token);
  vmo_name_ = std::move(vmo_name);
  create_buffer_collection_callback_ = std::move(callback);

  MaybeRespond();
}

void FakeBufferProvider::GetBuffers(zx::eventpair participant_token,
                                    fuchsia::media2::BufferConstraints constraints,
                                    fuchsia::media2::BufferRights rights, std::string name,
                                    uint64_t id, GetBuffersCallback callback) {
  FX_CHECK(participant_token);
  FX_CHECK(constraints.has_buffer_count());
  FX_CHECK(constraints.buffer_count() != 0);
  FX_CHECK(constraints.has_min_buffer_size());
  FX_CHECK(constraints.min_buffer_size() > 0);

  FX_CHECK(!participant_token_);

  participant_token_ = std::move(participant_token);
  constraints_ = std::move(constraints);
  rights_ = rights;
  get_buffers_callback_ = std::move(callback);

  MaybeRespond();
}

void FakeBufferProvider::MaybeRespond() {
  if (!provider_token_ || !participant_token_) {
    return;
  }

  FX_CHECK(GetKoid(provider_token_) == GetPeerKoid(participant_token_));
  FX_CHECK(create_buffer_collection_callback_);
  FX_CHECK(get_buffers_callback_);

  std::vector<zx::vmo> buffers(constraints_.buffer_count() + 1);
  for (auto& vmo : buffers) {
    auto status = zx::vmo::create(constraints_.min_buffer_size(), 0, &vmo);
    FX_CHECK(status == ZX_OK);
  }

  get_buffers_callback_(fpromise::ok(std::move(buffers)));

  fuchsia::media2::BufferCollectionInfo collection_info;
  collection_info.set_buffer_size(constraints_.min_buffer_size());
  collection_info.set_buffer_count(constraints_.buffer_count() + 1);
  create_buffer_collection_callback_(fpromise::ok(std::move(collection_info)));

  provider_token_.reset();
  participant_token_.reset();
  create_buffer_collection_callback_ = nullptr;
  get_buffers_callback_ = nullptr;
}

}  // namespace fmlib
