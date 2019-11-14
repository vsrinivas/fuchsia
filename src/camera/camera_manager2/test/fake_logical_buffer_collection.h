// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_CAMERA_MANAGER2_TEST_FAKE_LOGICAL_BUFFER_COLLECTION_H_
#define SRC_CAMERA_CAMERA_MANAGER2_TEST_FAKE_LOGICAL_BUFFER_COLLECTION_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>

#include <src/lib/syslog/cpp/logger.h>

namespace camera {
class FakeLogicalBufferCollection : public fuchsia::sysmem::BufferCollection {
 public:
  FakeLogicalBufferCollection(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  // Do nothing on getting constraints.
  void SetConstraints(bool has_constraints,
                      fuchsia::sysmem::BufferCollectionConstraints constraints) {}

  // Immediately call the callback when the client waits for buffers to be allocated.
  // The buffer collection passed back through the callback is empty.
  void WaitForBuffersAllocated(
      fit::function<void(int32_t, fuchsia::sysmem::BufferCollectionInfo_2)> callback);

  void Close() { closed_ = true; }

  // If |give_error| is set to true, subsequent calls to WaitForBuffersAllocated will return
  // an error.
  void SetBufferError(bool give_error) { give_error_ = give_error; }

  bool closed() { return closed_; }

  // Produce the client side of a connection to this server. This can only be called once.
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollection> GetBufferCollection();

  // These functions are here for the class to compile, but we do not use them:
  void SetEventSink(fidl::InterfaceHandle<class fuchsia::sysmem::BufferCollectionEvents> events) {}
  void CheckBuffersAllocated(fit::function<void(int32_t)> callback) {}
  void CloseSingleBuffer(uint64_t buffer_index) {}
  void AllocateSingleBuffer(uint64_t buffer_index) {}
  void WaitForSingleBufferAllocated(
      uint64_t buffer_index,
      fit::function<void(int32_t, fuchsia::sysmem::SingleBufferInfo)> callback) {}
  void CheckSingleBufferAllocated(uint64_t buffer_index) {}
  void Sync(fit::function<void()> callback) {}

 private:
  bool closed_ = true;
  bool give_error_ = false;
  async_dispatcher_t* dispatcher_;
  fidl::Binding<fuchsia::sysmem::BufferCollection> binding_{this};
};
}  // namespace camera

#endif  // SRC_CAMERA_CAMERA_MANAGER2_TEST_FAKE_LOGICAL_BUFFER_COLLECTION_H_
