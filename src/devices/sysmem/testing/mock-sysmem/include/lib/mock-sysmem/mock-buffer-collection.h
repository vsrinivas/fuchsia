// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_TESTING_MOCK_SYSMEM_INCLUDE_LIB_MOCK_SYSMEM_MOCK_BUFFER_COLLECTION_H_
#define SRC_DEVICES_SYSMEM_TESTING_MOCK_SYSMEM_INCLUDE_LIB_MOCK_SYSMEM_MOCK_BUFFER_COLLECTION_H_

#include <fuchsia/sysmem/llcpp/fidl.h>

#include "zxtest/zxtest.h"

namespace mock_sysmem {

class MockBufferCollection : public fidl::WireServer<fuchsia_sysmem::BufferCollection> {
 public:
  void SetEventSink(SetEventSinkRequestView request,
                    SetEventSinkCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void Sync(SyncRequestView request, SyncCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void SetConstraints(SetConstraintsRequestView request,
                      SetConstraintsCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void WaitForBuffersAllocated(WaitForBuffersAllocatedRequestView request,
                               WaitForBuffersAllocatedCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void CheckBuffersAllocated(CheckBuffersAllocatedRequestView request,
                             CheckBuffersAllocatedCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void CloseSingleBuffer(CloseSingleBufferRequestView request,
                         CloseSingleBufferCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void AllocateSingleBuffer(AllocateSingleBufferRequestView request,
                            AllocateSingleBufferCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void WaitForSingleBufferAllocated(
      WaitForSingleBufferAllocatedRequestView request,
      WaitForSingleBufferAllocatedCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void CheckSingleBufferAllocated(CheckSingleBufferAllocatedRequestView request,
                                  CheckSingleBufferAllocatedCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void Close(CloseRequestView request, CloseCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void SetName(SetNameRequestView request, SetNameCompleter::Sync& completer) override {}
  void SetDebugClientInfo(SetDebugClientInfoRequestView request,
                          SetDebugClientInfoCompleter::Sync& completer) override {}
  void SetConstraintsAuxBuffers(SetConstraintsAuxBuffersRequestView request,
                                SetConstraintsAuxBuffersCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void GetAuxBuffers(GetAuxBuffersRequestView request,
                     GetAuxBuffersCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
};

}  // namespace mock_sysmem

#endif  // SRC_DEVICES_SYSMEM_TESTING_MOCK_SYSMEM_INCLUDE_LIB_MOCK_SYSMEM_MOCK_BUFFER_COLLECTION_H_
