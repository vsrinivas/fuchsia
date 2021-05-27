// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/graph/test/fake_service_provider.h"

namespace media_player::test {

//////////////////////////////////////////////////////////////////////////////////////////////
// FakeBufferCollection implementation.

FakeBufferCollection::FakeBufferCollection(FakeServiceProvider* owner) : owner_(owner) {
  bindings_.set_empty_set_handler([this]() { owner_->RemoveCollection(this); });
}

FakeBufferCollection::~FakeBufferCollection() {}

void FakeBufferCollection::Bind(fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection> request) {
  bindings_.AddBinding(this, std::move(request));
}

void FakeBufferCollection::SetBufferCollection(
    zx_status_t status, fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info) {
  EXPECT_EQ(ZX_ERR_UNAVAILABLE, buffer_allocation_status_);
  buffer_allocation_status_ = status;
  buffer_collection_info_ = std::move(buffer_collection_info);
  for (auto& wait_callback : wait_callbacks_) {
    wait_callback(buffer_allocation_status_, fidl::Clone(buffer_collection_info_));
  }
  wait_callbacks_.clear();
}

void FakeBufferCollection::Sync(SyncCallback callback) { callback(); }

void FakeBufferCollection::SetConstraints(
    bool has_constraints, fuchsia::sysmem::BufferCollectionConstraints constraints) {
  constraints_.push_back(constraints);
}

void FakeBufferCollection::WaitForBuffersAllocated(WaitForBuffersAllocatedCallback callback) {
  if (buffer_allocation_status_ == ZX_ERR_UNAVAILABLE) {
    wait_callbacks_.push_back(std::move(callback));
    return;
  }

  callback(buffer_allocation_status_, fidl::Clone(buffer_collection_info_));
}

void FakeBufferCollection::CheckBuffersAllocated(CheckBuffersAllocatedCallback callback) {
  callback(buffer_allocation_status_);
}

void FakeBufferCollection::NotImplemented_(const std::string& name) { FX_NOTIMPLEMENTED() << name; }

//////////////////////////////////////////////////////////////////////////////////////////////
// FakeBufferCollectionToken implementation.

FakeBufferCollectionToken::FakeBufferCollectionToken(FakeServiceProvider* owner) : owner_(owner) {
  bindings_.set_empty_set_handler([this]() { owner_->RemoveToken(this); });
}

FakeBufferCollectionToken::~FakeBufferCollectionToken() {}

void FakeBufferCollectionToken::Bind(
    fidl::InterfaceRequest<fuchsia::sysmem::BufferCollectionToken> request) {
  owner_->AddTokenBinding(this, request.channel());
  bindings_.AddBinding(this, std::move(request));
}

void FakeBufferCollectionToken::Duplicate(
    uint32_t rights_attenuation_mask,
    fidl::InterfaceRequest<fuchsia::sysmem::BufferCollectionToken> request) {
  Bind(std::move(request));
}

void FakeBufferCollectionToken::Sync(SyncCallback callback) { callback(); }

void FakeBufferCollectionToken::NotImplemented_(const std::string& name) {
  FX_NOTIMPLEMENTED() << name;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FakeServiceProvider implementation.

FakeBufferCollection* FakeServiceProvider::GetCollectionFromToken(
    fuchsia::sysmem::BufferCollectionTokenPtr token) {
  EXPECT_TRUE(!!token);
  return FindOrCreateCollectionForToken(token.Unbind().TakeChannel());
}

FakeServiceProvider::~FakeServiceProvider() {}

void FakeServiceProvider::AddTokenBinding(FakeBufferCollectionToken* token,
                                          const zx::channel& channel) {
  zx_info_handle_basic_t info;
  EXPECT_EQ(ZX_OK, channel.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
  tokens_by_server_koid_.emplace(info.koid, token);
}

void FakeServiceProvider::RemoveToken(FakeBufferCollectionToken* token) {
  EXPECT_EQ(1u, tokens_.erase(token));

  for (auto iter = tokens_by_server_koid_.begin(); iter != tokens_by_server_koid_.end();) {
    if (iter->second == token) {
      iter = tokens_by_server_koid_.erase(iter);
    } else {
      ++iter;
    }
  }
}

void FakeServiceProvider::RemoveCollection(FakeBufferCollection* collection) {
  EXPECT_EQ(1u, collections_.erase(collection));
}

void FakeServiceProvider::ConnectToService(std::string service_path, zx::channel channel) {
  EXPECT_EQ(fuchsia::sysmem::Allocator::Name_, service_path);
  EXPECT_TRUE(!!channel);
  bindings_.AddBinding(this,
                       fidl::InterfaceRequest<fuchsia::sysmem::Allocator>(std::move(channel)));
}

void FakeServiceProvider::AllocateSharedCollection(
    fidl::InterfaceRequest<fuchsia::sysmem::BufferCollectionToken> token_request) {
  EXPECT_TRUE(!!token_request);

  auto token = std::make_unique<FakeBufferCollectionToken>(this);
  auto token_raw_ptr = token.get();
  tokens_.emplace(token_raw_ptr, std::move(token));
  token_raw_ptr->Bind(std::move(token_request));
}

void FakeServiceProvider::BindSharedCollection(
    fidl::InterfaceHandle<class fuchsia::sysmem::BufferCollectionToken> token_handle,
    fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection> buffer_collection_request) {
  FindOrCreateCollectionForToken(token_handle.TakeChannel())
      ->Bind(std::move(buffer_collection_request));
}

void FakeServiceProvider::ValidateBufferCollectionToken(
    uint64_t token_server_koid,
    fuchsia::sysmem::Allocator::ValidateBufferCollectionTokenCallback callback) {
  callback(tokens_by_server_koid_.find(token_server_koid) != tokens_by_server_koid_.end());
}

void FakeServiceProvider::NotImplemented_(const std::string& name) { FX_NOTIMPLEMENTED() << name; }

FakeBufferCollection* FakeServiceProvider::FindOrCreateCollectionForToken(
    zx::channel client_channel) {
  zx_info_handle_basic_t info;
  EXPECT_EQ(ZX_OK,
            client_channel.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
  auto iter = tokens_by_server_koid_.find(info.related_koid);
  EXPECT_NE(tokens_by_server_koid_.end(), iter);

  auto token = iter->second;

  // This connection will be closing when |client_channel| goes out of scope.
  tokens_by_server_koid_.erase(iter);

  // Find/create the collection.
  FakeBufferCollection* collection_raw_ptr;

  auto collection_iter = collections_by_token_.find(token);
  if (collection_iter == collections_by_token_.end()) {
    auto collection = std::make_unique<FakeBufferCollection>(this);
    collection_raw_ptr = collection.get();
    collections_.emplace(collection_raw_ptr, std::move(collection));
    collections_by_token_.emplace(token, collection_raw_ptr);
  } else {
    collection_raw_ptr = collection_iter->second;
  }

  return collection_raw_ptr;
}

}  // namespace media_player::test
