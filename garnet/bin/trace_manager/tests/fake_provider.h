// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_MANAGER_TESTS_FAKE_PROVIDER_H_
#define GARNET_BIN_TRACE_MANAGER_TESTS_FAKE_PROVIDER_H_

#include <fuchsia/tracing/controller/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/trace-engine/buffer_internal.h>
#include <lib/zx/fifo.h>

#include <gtest/gtest.h>
#include <trace-provider/provider.h>
#include <trace-reader/reader.h>

namespace tracing {
namespace test {

namespace provider = ::fuchsia::tracing::provider;

class FakeProvider : public provider::Provider {
 public:
  // Track the last request made.
  enum State {
    // Provider has not received any requests yet.
    kReady,
    // Upon receipt of |Initialize()| transition immediately to |kInitialized|.
    kInitialized,
    // Have received |Start()| but have not started yet.
    kStarting,
    // Provider has started tracing.
    kStarted,
    // Have received |Stop()| but have not stopped yet.
    kStopping,
    // Provider has stopped tracing.
    kStopped,
    // Have received |Terminate()| but have not terminated yet.
    kTerminating,
    // Provider has terminated tracing.
    // There is no transition from |kTerminated| to |kReady| in the tests.
    kTerminated,
  };

  static constexpr size_t kHeaderSize = sizeof(trace::internal::trace_buffer_header);

  // The size of our durable buffer in CIRCULAR,STREAMING modes.
  static constexpr size_t kDurableBufferSize = 4096;

  FakeProvider(zx_koid_t pid, const std::string& name);

  std::string PrettyName() const;

  // |Provider| implementation.
  void Initialize(provider::ProviderConfig config) override;
  void Start(provider::StartOptions options) override;
  void Stop() override;
  void Terminate() override;

  // Helpers to provide discrete advancement of provider state.
  // These should only be called when the provider is in the preceding state,
  // e.g., kStarting, kStopping, kTerminating.
  void MarkStarted();
  void MarkStopped();
  void MarkTerminated();

  // Raw state advancement.
  // This should only be called under exceptional circumstances, e.g., to
  // test the handling of broken providers.
  void AdvanceToState(State state);

  zx_koid_t pid() const { return pid_; }
  const std::string& name() const { return name_; }

  State state() const { return state_; }

  int initialize_count() const { return initialize_count_; }
  int start_count() const { return start_count_; }
  int stop_count() const { return stop_count_; }
  int terminate_count() const { return terminate_count_; }

 private:
  friend std::ostream& operator<<(std::ostream& out, FakeProvider::State state);

  bool SendFifoPacket(const trace_provider_packet_t* packet);

  void InitializeBuffer();

  // These functions have the same names as their trace-engine counterparts.
  void ComputeBufferSizes();
  void ResetBufferPointers();
  void InitBufferHeader();
  void UpdateBufferHeaderAfterStopped();

  void WriteInitRecord();
  void WriteBlobRecord();
  void WriteRecordToBuffer(const uint8_t* data, size_t size);
  void WriteZeroLengthRecord(size_t offset);
  void WriteBytes(const uint8_t* data, size_t offset, size_t size);

  const zx_koid_t pid_;
  const std::string name_;

  State state_ = State::kReady;

  provider::BufferingMode buffering_mode_;
  zx::vmo buffer_vmo_;
  zx::fifo fifo_;
  std::vector<std::string> categories_;

  size_t total_buffer_size_ = 0;
  size_t durable_buffer_size_ = 0;
  size_t rolling_buffer_size_ = 0;
  size_t buffer_next_ = 0;

  int initialize_count_ = 0;
  int start_count_ = 0;
  int stop_count_ = 0;
  int terminate_count_ = 0;
};

using FakeProviderBinding = fidl::Binding<provider::Provider, std::unique_ptr<FakeProvider>>;

}  // namespace test
}  // namespace tracing

#endif  // GARNET_BIN_TRACE_MANAGER_TESTS_FAKE_PROVIDER_H_
