// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_FAKE_ENDPOINT_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_FAKE_ENDPOINT_H_

#include <fuchsia/netemul/network/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/stdcompat/optional.h>

#include <memory>
#include <queue>

#include "src/connectivity/network/testing/netemul/lib/network/consumer.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace netemul {

class FakeEndpoint : public fuchsia::netemul::network::FakeEndpoint, public data::Consumer {
 public:
  // The maximum number of frames a FakeEndpoint keeps before dropping the oldest one.
  static constexpr uint64_t kMaxPendingFrames = 1024;
  using FFakeEndpoint = fuchsia::netemul::network::FakeEndpoint;
  using OnDisconnectedCallback = fit::function<void()>;

  explicit FakeEndpoint(data::BusConsumer::Ptr sink, fidl::InterfaceRequest<FFakeEndpoint> request,
                        async_dispatcher_t* dispatcher = nullptr);

  // Called when the FIDL binding is voided
  void SetOnDisconnected(OnDisconnectedCallback cl);

  fxl::WeakPtr<data::Consumer> GetPointer();

  // fidl interface implementations:
  void Write(::std::vector<uint8_t> data, WriteCallback callback) override;
  void Read(ReadCallback callback) override;
  // data consumer implementations:
  void Consume(const void* data, size_t len) override;

 private:
  void PopReadQueue();

  OnDisconnectedCallback on_disconnected_;
  data::BusConsumer::Ptr sink_;
  fidl::Binding<FFakeEndpoint> binding_;
  fxl::WeakPtrFactory<data::Consumer> weak_ptr_factory_;
  std::queue<std::vector<uint8_t>> pending_frames_;
  cpp17::optional<ReadCallback> pending_callback_;
  uint64_t dropped_;
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_FAKE_ENDPOINT_H_
