// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NETEMUL_NETWORK_FAKE_ENDPOINT_H_
#define LIB_NETEMUL_NETWORK_FAKE_ENDPOINT_H_

#include <fuchsia/netemul/network/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <src/lib/fxl/memory/weak_ptr.h>
#include <memory>
#include "lib/netemul/network/consumer.h"

namespace netemul {

class FakeEndpoint : public fuchsia::netemul::network::FakeEndpoint,
                     public data::Consumer {
 public:
  using FFakeEndpoint = fuchsia::netemul::network::FakeEndpoint;
  using Ptr = std::unique_ptr<FakeEndpoint>;
  using OnDisconnectedCallback = fit::function<void(const FakeEndpoint*)>;

  explicit FakeEndpoint(data::BusConsumer::Ptr sink,
                        fidl::InterfaceRequest<FFakeEndpoint> request,
                        async_dispatcher_t* dispatcher = nullptr);

  // Called when the FIDL binding is voided
  void SetOnDisconnected(OnDisconnectedCallback cl);

  fxl::WeakPtr<data::Consumer> GetPointer();

  // fidl interface implementations:
  void Write(::std::vector<uint8_t> data) override;
  // data consumer implementations:
  void Consume(const void* data, size_t len) override;

 private:
  OnDisconnectedCallback on_disconnected_;
  data::BusConsumer::Ptr sink_;
  fidl::Binding<FFakeEndpoint> binding_;
  fxl::WeakPtrFactory<data::Consumer> weak_ptr_factory_;
};

}  // namespace netemul

#endif  // LIB_NETEMUL_NETWORK_FAKE_ENDPOINT_H_
