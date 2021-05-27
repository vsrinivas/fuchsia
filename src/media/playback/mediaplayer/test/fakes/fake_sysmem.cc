// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/test/fakes/fake_sysmem.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/comparison.h>
#include <lib/syslog/cpp/macros.h>

#include "src/media/playback/mediaplayer/test/fakes/formatting.h"

namespace media_player {
namespace test {

FakeSysmem::FakeSysmem() : dispatcher_(async_get_default_dispatcher()) {}

FakeSysmem::~FakeSysmem() {}

bool FakeSysmem::expected() const {
  if (!expectations_.has_value()) {
    return true;
  }

  if (!expected_) {
    return false;
  }

  if (!expectations_.value().empty()) {
    FX_LOGS(ERROR) << expectations_.value().size() << " expected collection(s) never bound";
    return false;
  }

  for (auto& pair : collections_by_token_) {
    if (!pair.second->expected()) {
      return false;
    }
  }

  return true;
}

void FakeSysmem::RemoveToken(FakeBufferCollectionToken* token) {
  auto count = tokens_.erase(token);
  FX_CHECK(count == 1);
  auto iter = collections_by_token_.find(token);
  if (iter != collections_by_token_.end()) {
    auto collection = std::move(iter->second);
    collection->AllParticipantsBound();

    // Move the collection from |collections_by_token_| to |bound_collections_|, so
    // we don't identify it with a new token with the same address as |token|.
    collections_by_token_.erase(iter);
    auto raw_collection = collection.get();
    bound_collections_.emplace(raw_collection, std::move(collection));
  }
}

void FakeSysmem::RemoveCollection(FakeBufferCollection* collection) {
  auto count = bound_collections_.erase(collection);
  if (count == 1) {
    return;
  }

  for (auto iter = collections_by_token_.begin(); iter != collections_by_token_.end(); ++iter) {
    if (iter->second.get() == collection) {
      collections_by_token_.erase(iter);
      return;
    }
  }

  FX_CHECK(false) << "RemoveCollection called for unrecognized collection";
}

void FakeSysmem::AllocateSharedCollection(
    fidl::InterfaceRequest<fuchsia::sysmem::BufferCollectionToken> token_request) {
  auto token = std::make_unique<FakeBufferCollectionToken>(this);
  token->Bind(std::move(token_request));
  auto raw_token_ptr = token.get();
  tokens_.emplace(raw_token_ptr, std::move(token));
}

void FakeSysmem::BindSharedCollection(
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
    fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection> buffer_collection_request) {
  zx_info_handle_basic_t info;
  auto status =
      token.channel().get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  FX_CHECK(status == ZX_OK);

  for (auto& pair : tokens_) {
    if (pair.first->HoldsBinding(info.related_koid)) {
      auto iter = collections_by_token_.find(pair.first);
      if (iter == collections_by_token_.end()) {
        std::unique_ptr<FakeBufferCollection> collection;
        if (expectations_.has_value()) {
          if (expectations_.value().empty()) {
            FX_LOGS(ERROR) << "Unexpected call to BindSharedCollection, request dropped.";
            expected_ = false;
            return;
          }

          FX_CHECK(expectations_.value().front());
          collection = std::make_unique<FakeBufferCollection>(
              this, next_collection_id_++, std::move(expectations_.value().front()),
              dump_expectations_);
          expectations_.value().pop_front();
        } else {
          collection = std::make_unique<FakeBufferCollection>(this, next_collection_id_++, nullptr,
                                                              dump_expectations_);
        }

        collection->Bind(std::move(buffer_collection_request));
        collections_by_token_.emplace(pair.first, std::move(collection));
      } else {
        iter->second->Bind(std::move(buffer_collection_request));
      }

      return;
    }
  }

  FX_CHECK(false) << "BindSharedCollection: token not recognized";
}

void FakeSysmem::NotImplemented_(const std::string& name) { FX_NOTIMPLEMENTED() << name; }

////////////////////////////////////////////////////////////////////////////////
// FakeBufferCollectionToken implementation.

FakeBufferCollectionToken::FakeBufferCollectionToken(FakeSysmem* owner) : owner_(owner) {
  bindings_.set_empty_set_handler([this]() {
    bindings_.set_empty_set_handler(nullptr);
    owner_->RemoveToken(this);
  });
}

FakeBufferCollectionToken::~FakeBufferCollectionToken() {}

bool FakeBufferCollectionToken::HoldsBinding(zx_koid_t koid) {
  for (auto& binding : bindings_.bindings()) {
    zx_info_handle_basic_t info;
    auto status =
        binding->channel().get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    FX_CHECK(status == ZX_OK);
    if (info.koid == koid) {
      return true;
    }
  }

  return false;
}

void FakeBufferCollectionToken::Bind(
    fidl::InterfaceRequest<fuchsia::sysmem::BufferCollectionToken> request) {
  bindings_.AddBinding(this, std::move(request));
}

void FakeBufferCollectionToken::Duplicate(
    uint32_t rights_attenuation_mask,
    fidl::InterfaceRequest<fuchsia::sysmem::BufferCollectionToken> token_request) {
  Bind(std::move(token_request));
}

void FakeBufferCollectionToken::Sync(SyncCallback callback) { callback(); }

void FakeBufferCollectionToken::Close() {}

void FakeBufferCollectionToken::SetDebugClientInfo(std::string name, uint64_t id) {}

void FakeBufferCollectionToken::SetDebugTimeoutLogDeadline(int64_t deadline) {}

void FakeBufferCollectionToken::NotImplemented_(const std::string& name) {
  FX_NOTIMPLEMENTED() << name;
}

////////////////////////////////////////////////////////////////////////////////
// FakeBufferCollection implementation.

FakeBufferCollection::FakeBufferCollection(FakeSysmem* owner, uint32_t id,
                                           std::unique_ptr<FakeSysmem::Expectations> expectations,
                                           bool dump_expectations)
    : owner_(owner),
      id_(id),
      expectations_(std::move(expectations)),
      dump_expectations_(dump_expectations) {
  bindings_.set_empty_set_handler([this]() { delete this; });
  bindings_.set_empty_set_handler([this]() {
    bindings_.set_empty_set_handler(nullptr);
    owner_->RemoveCollection(this);
  });
}

FakeBufferCollection::~FakeBufferCollection() {}

bool FakeBufferCollection::expected() const { return expected_; }

void FakeBufferCollection::AllParticipantsBound() {
  all_participants_bound_ = true;
  MaybeCompleteAllocation();
}

void FakeBufferCollection::Sync(SyncCallback callback) { callback(); }

void FakeBufferCollection::SetConstraints(
    bool has_constraints, fuchsia::sysmem::BufferCollectionConstraints constraints) {
  if (allocation_complete_) {
    FX_LOGS(ERROR) << "SetConstraints: called after allocation complete, id " << id_
                   << ", constraints " << constraints;
    expected_ = false;
    return;
  }

  if (dump_expectations_) {
    std::cerr << "// collection " << id_ << "\n";
    std::cerr << constraints << "\n";
  }

  if (expectations_) {
    bool found = false;
    for (auto iter = expectations_->constraints_.begin(); iter != expectations_->constraints_.end();
         ++iter) {
      if (fidl::Equals(constraints, *iter)) {
        expectations_->constraints_.erase(iter);
        found = true;
        break;
      }
    }

    if (!found) {
      FX_LOGS(ERROR) << "SetConstraints: constraints not expected " << constraints;
      expected_ = false;
    }
  }
}

void FakeBufferCollection::WaitForBuffersAllocated(WaitForBuffersAllocatedCallback callback) {
  waiter_callbacks_.push_back(std::move(callback));
  MaybeCompleteAllocation();
}

void FakeBufferCollection::Close() {}

void FakeBufferCollection::SetName(uint32_t priority, std::string name) {}

void FakeBufferCollection::SetDebugClientInfo(std::string name, uint64_t id) {}

void FakeBufferCollection::NotImplemented_(const std::string& name) { FX_NOTIMPLEMENTED() << name; }

void FakeBufferCollection::MaybeCompleteAllocation() {
  if (!all_participants_bound_ || waiter_callbacks_.size() != bindings_.size()) {
    return;
  }

  if (expectations_ && !expectations_->constraints_.empty()) {
    for (auto& constraints : expectations_->constraints_) {
      FX_LOGS(ERROR) << "WaitForBuffersAllocated: constraints not received " << constraints;
    }

    expected_ = false;
  }

  allocation_complete_ = true;

  while (!waiter_callbacks_.empty()) {
    if (expectations_) {
      waiter_callbacks_.back()(ZX_OK, fidl::Clone(expectations_->collection_info_));
    } else {
      FX_LOGS(ERROR) << "Lacking expectations required to answer WaitForBuffersAllocated";
    }

    waiter_callbacks_.pop_back();
  }
}

}  // namespace test
}  // namespace media_player
