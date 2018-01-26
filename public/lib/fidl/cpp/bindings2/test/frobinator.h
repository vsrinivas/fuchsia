// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS2_TEST_FROBINATOR_H_
#define LIB_FIDL_CPP_BINDINGS2_TEST_FROBINATOR_H_

#include "lib/fidl/cpp/bindings2/interface_ptr.h"
#include "lib/fidl/cpp/bindings2/internal/proxy_controller.h"
#include "lib/fidl/cpp/bindings2/internal/stub_controller.h"
#include "lib/fidl/cpp/bindings2/string.h"

namespace fidl {
namespace test {
class FrobinatorProxy;
class FrobinatorStub;

class Frobinator {
 public:
  using Proxy_ = FrobinatorProxy;
  using Stub_ = FrobinatorStub;
  virtual ~Frobinator();
  virtual void Frob(::fidl::StringPtr value) = 0;
  using GrobCallback = std::function<void(::fidl::StringPtr)>;
  virtual void Grob(::fidl::StringPtr value, GrobCallback callback) = 0;
};

using FrobinatorPtr = ::fidl::InterfacePtr<Frobinator>;

class FrobinatorProxy : public Frobinator {
 public:
  explicit FrobinatorProxy(::fidl::internal::ProxyController* controller);
  ~FrobinatorProxy() override;

  void Frob(::fidl::StringPtr value) override;
  void Grob(::fidl::StringPtr value, GrobCallback callback) override;

 private:
  FrobinatorProxy(const FrobinatorProxy&) = delete;
  FrobinatorProxy& operator=(const FrobinatorProxy&) = delete;

  ::fidl::internal::ProxyController* controller_;
};

class FrobinatorStub : public ::fidl::internal::Stub {
 public:
  explicit FrobinatorStub(Frobinator* impl);
  ~FrobinatorStub() override;

  zx_status_t Dispatch(::fidl::Message message,
                       ::fidl::internal::PendingResponse response) override;

 private:
  Frobinator* impl_;
};

}  // namespace test
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS2_TEST_FROBINATOR_H_
