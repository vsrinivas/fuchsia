// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_TESTING_MOCK_SYSMEM_INCLUDE_LIB_MOCK_SYSMEM_MOCK_BUFFER_COLLECTION_H_
#define SRC_DEVICES_SYSMEM_TESTING_MOCK_SYSMEM_INCLUDE_LIB_MOCK_SYSMEM_MOCK_BUFFER_COLLECTION_H_

#include <fuchsia/sysmem/llcpp/fidl.h>

#include "zxtest/zxtest.h"

namespace mock_sysmem {

class MockBufferCollection : public llcpp::fuchsia::sysmem::BufferCollection::Interface {
 public:
  void SetEventSink(fidl::ClientEnd<llcpp::fuchsia::sysmem::BufferCollectionEvents> events,
                    SetEventSinkCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void Sync(SyncCompleter::Sync& _completer) override { EXPECT_TRUE(false); }
  void SetConstraints(bool has_constraints,
                      llcpp::fuchsia::sysmem::wire::BufferCollectionConstraints constraints,
                      SetConstraintsCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void WaitForBuffersAllocated(WaitForBuffersAllocatedCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void CheckBuffersAllocated(CheckBuffersAllocatedCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void CloseSingleBuffer(uint64_t buffer_index,
                         CloseSingleBufferCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void AllocateSingleBuffer(uint64_t buffer_index,
                            AllocateSingleBufferCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void WaitForSingleBufferAllocated(
      uint64_t buffer_index, WaitForSingleBufferAllocatedCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void CheckSingleBufferAllocated(uint64_t buffer_index,
                                  CheckSingleBufferAllocatedCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void Close(CloseCompleter::Sync& _completer) override { EXPECT_TRUE(false); }
  void SetName(uint32_t priority, fidl::StringView name,
               SetNameCompleter::Sync& completer) override {}
  void SetDebugClientInfo(fidl::StringView name, uint64_t id,
                          SetDebugClientInfoCompleter::Sync& completer) override {}
  void SetConstraintsAuxBuffers(
      llcpp::fuchsia::sysmem::wire::BufferCollectionConstraintsAuxBuffers constraints,
      SetConstraintsAuxBuffersCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void GetAuxBuffers(GetAuxBuffersCompleter::Sync& _completer) override { EXPECT_TRUE(false); }
};

}  // namespace mock_sysmem

#endif  // SRC_DEVICES_SYSMEM_TESTING_MOCK_SYSMEM_INCLUDE_LIB_MOCK_SYSMEM_MOCK_BUFFER_COLLECTION_H_
