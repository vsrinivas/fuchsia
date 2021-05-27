// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_SYSMEM_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_SYSMEM_H_

#include <fuchsia/sysmem/cpp/fidl_test_base.h>
#include <lib/async/dispatcher.h>

#include <list>
#include <unordered_map>

#include "lib/fidl/cpp/binding_set.h"

namespace media_player {
namespace test {

class FakeBufferCollectionToken;
class FakeBufferCollection;

// Implements sysmem for testing.
class FakeSysmem : public fuchsia::sysmem::testing::Allocator_TestBase {
 public:
  // Expectations relating to a single buffer collection requested using
  // |Allocator::AllocateSharedCollection|. |constraints_| are constraints that are expected to be
  // applied using |BufferCollection::SetConstraints|. Constraints may be supplied in any order.
  // |collection_info_| specifies the collection produced by
  // |BufferCollection::WaitForBuffersAllocated|.
  struct Expectations {
    std::list<fuchsia::sysmem::BufferCollectionConstraints> constraints_;
    fuchsia::sysmem::BufferCollectionInfo_2 collection_info_;
  };

  FakeSysmem();

  ~FakeSysmem() override;

  // Establishes expectations regarding collections that will be created and the constraints that
  // will be applied to those collections. Also specifies the buffer collection to be produced in
  // each case.
  //
  // Each item in the |expectations| list corresponds to a collection in the order they will be
  // created using |Allocator::BindSharedCollection|. The ordering constraint applies to the first
  // call to |BindSharedCollection| for a given collection. If that ordering cannot be predicted,
  // this fake will not work. See |Expectations| for details.
  //
  // Note that the fake collections cannot return buffer collections unless expectations are set.
  void SetExpectations(std::list<std::unique_ptr<Expectations>> expectations) {
    expectations_ = std::move(expectations);
  }

  // Whether expectations have been met. Returns true if |SetExpectations| was never called.
  bool expected() const;

  // Causes this fake to print C++ literals for constraints to be used for |SetExpectations|.
  void DumpExpectations() { dump_expectations_ = true; }

  // Returns a request handler for binding to this fake service.
  fidl::InterfaceRequestHandler<fuchsia::sysmem::Allocator> GetRequestHandler() {
    return bindings_.GetHandler(this);
  }

  // Binds this service.
  void Bind(fidl::InterfaceRequest<fuchsia::sysmem::Allocator> request) {
    bindings_.AddBinding(this, std::move(request));
  }

  void RemoveToken(FakeBufferCollectionToken* token);

  void RemoveCollection(FakeBufferCollection* collection);

  // Allocator implementation.
  void AllocateSharedCollection(
      fidl::InterfaceRequest<fuchsia::sysmem::BufferCollectionToken> token_request) override;

  void BindSharedCollection(
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
      fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection> buffer_collection_request) override;

  void NotImplemented_(const std::string& name) override;

 private:
  async_dispatcher_t* dispatcher_;
  fidl::BindingSet<fuchsia::sysmem::Allocator> bindings_;
  std::optional<std::list<std::unique_ptr<Expectations>>> expectations_;
  bool expected_ = true;
  bool dump_expectations_ = false;
  std::unordered_map<FakeBufferCollectionToken*, std::unique_ptr<FakeBufferCollectionToken>>
      tokens_;
  std::unordered_map<FakeBufferCollectionToken*, std::unique_ptr<FakeBufferCollection>>
      collections_by_token_;
  std::unordered_map<FakeBufferCollection*, std::unique_ptr<FakeBufferCollection>>
      bound_collections_;
  uint32_t next_collection_id_;

  // Disallow copy, assign and move.
  FakeSysmem(const FakeSysmem&) = delete;
  FakeSysmem(FakeSysmem&&) = delete;
  FakeSysmem& operator=(const FakeSysmem&) = delete;
  FakeSysmem& operator=(FakeSysmem&&) = delete;
};

class FakeBufferCollectionToken : public fuchsia::sysmem::testing::BufferCollectionToken_TestBase {
 public:
  FakeBufferCollectionToken(FakeSysmem* owner);

  ~FakeBufferCollectionToken() override;

  bool HoldsBinding(zx_koid_t koid);

  // Binds this service.
  void Bind(fidl::InterfaceRequest<fuchsia::sysmem::BufferCollectionToken> request);

  // BufferCollectionToken implementation.
  void Duplicate(
      uint32_t rights_attenuation_mask,
      fidl::InterfaceRequest<fuchsia::sysmem::BufferCollectionToken> token_request) override;

  void Sync(SyncCallback callback) override;

  void Close() override;

  void SetDebugClientInfo(std::string name, uint64_t id) override;

  void SetDebugTimeoutLogDeadline(int64_t deadline) override;

  void NotImplemented_(const std::string& name) override;

 private:
  FakeSysmem* owner_;
  fidl::BindingSet<fuchsia::sysmem::BufferCollectionToken> bindings_;

  // Disallow copy, assign and move.
  FakeBufferCollectionToken(const FakeBufferCollectionToken&) = delete;
  FakeBufferCollectionToken(FakeBufferCollectionToken&&) = delete;
  FakeBufferCollectionToken& operator=(const FakeBufferCollectionToken&) = delete;
  FakeBufferCollectionToken& operator=(FakeBufferCollectionToken&&) = delete;
};

class FakeBufferCollection : public fuchsia::sysmem::testing::BufferCollection_TestBase {
 public:
  FakeBufferCollection(FakeSysmem* owner, uint32_t id,
                       std::unique_ptr<FakeSysmem::Expectations> expectations,
                       bool dump_expectations);

  ~FakeBufferCollection() override;

  uint32_t id() const { return id_; };

  // Whether expectations have been met. Returns true if |expectations| was null in the constructor.
  bool expected() const;

  // Binds this service.
  void Bind(fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection> request) {
    bindings_.AddBinding(this, std::move(request));
  }

  void AllParticipantsBound();

  // BufferCollection implementation.
  void Sync(SyncCallback callback) override;

  void SetConstraints(bool has_constraints,
                      fuchsia::sysmem::BufferCollectionConstraints constraints) override;

  void WaitForBuffersAllocated(WaitForBuffersAllocatedCallback callback) override;

  void Close() override;

  void SetName(uint32_t priority, std::string name) override;

  void SetDebugClientInfo(std::string name, uint64_t id) override;

  void NotImplemented_(const std::string& name) override;

 private:
  void MaybeCompleteAllocation();

  FakeSysmem* owner_;
  uint32_t id_;
  std::unique_ptr<FakeSysmem::Expectations> expectations_;
  bool expected_ = true;
  bool dump_expectations_ = false;
  fidl::BindingSet<fuchsia::sysmem::BufferCollection> bindings_;
  std::list<WaitForBuffersAllocatedCallback> waiter_callbacks_;
  bool all_participants_bound_ = false;
  bool allocation_complete_ = false;

  // Disallow copy, assign and move.
  FakeBufferCollection(const FakeBufferCollection&) = delete;
  FakeBufferCollection(FakeBufferCollection&&) = delete;
  FakeBufferCollection& operator=(const FakeBufferCollection&) = delete;
  FakeBufferCollection& operator=(FakeBufferCollection&&) = delete;
};

}  // namespace test
}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_SYSMEM_H_
