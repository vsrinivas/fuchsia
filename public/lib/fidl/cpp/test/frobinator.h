// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_TEST_FROBINATOR_H_
#define LIB_FIDL_CPP_TEST_FROBINATOR_H_

#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/internal/proxy_controller.h"
#include "lib/fidl/cpp/internal/stub_controller.h"
#include "lib/fidl/cpp/internal/synchronous_proxy.h"
#include "lib/fidl/cpp/string.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"

namespace fidl {
namespace test {
class Frobinator_Proxy;
class Frobinator_Stub;
class Frobinator_Sync;
class Frobinator_SyncProxy;

class Frobinator {
 public:
  using Proxy_ = Frobinator_Proxy;
  using Stub_ = Frobinator_Stub;
  using Sync_ = Frobinator_Sync;
  virtual ~Frobinator();
  virtual void Frob(::fidl::StringPtr value) = 0;
  using GrobCallback = std::function<void(::fidl::StringPtr)>;
  virtual void Grob(::fidl::StringPtr value, GrobCallback callback) = 0;
};

class Frobinator_Sync {
 public:
  using Proxy_ = Frobinator_SyncProxy;
  virtual ~Frobinator_Sync();
  virtual zx_status_t Frob(::fidl::StringPtr value) = 0;
  virtual zx_status_t Grob(::fidl::StringPtr value,
                           ::fidl::StringPtr* out_result) = 0;
};

using FrobinatorPtr = ::fidl::InterfacePtr<Frobinator>;
using FrobinatorSyncPtr = ::fidl::SynchronousInterfacePtr<Frobinator>;

class Frobinator_Proxy : public Frobinator {
 public:
  explicit Frobinator_Proxy(::fidl::internal::ProxyController* controller);
  ~Frobinator_Proxy() override;

  void Frob(::fidl::StringPtr value) override;
  void Grob(::fidl::StringPtr value, GrobCallback callback) override;

 private:
  Frobinator_Proxy(const Frobinator_Proxy&) = delete;
  Frobinator_Proxy& operator=(const Frobinator_Proxy&) = delete;

  ::fidl::internal::ProxyController* controller_;
};

class Frobinator_Stub : public ::fidl::internal::Stub {
 public:
  explicit Frobinator_Stub(Frobinator* impl);
  ~Frobinator_Stub() override;

  zx_status_t Dispatch(::fidl::Message message,
                       ::fidl::internal::PendingResponse response) override;

 private:
  Frobinator* impl_;
};

class Frobinator_SyncProxy : public Frobinator_Sync {
 public:
  Frobinator_SyncProxy(zx::channel channel);
  ~Frobinator_SyncProxy() override;

  ::fidl::internal::SynchronousProxy* proxy() { return &proxy_; }

  zx_status_t Frob(::fidl::StringPtr value) override;
  zx_status_t Grob(::fidl::StringPtr value,
                   ::fidl::StringPtr* out_result) override;

 private:
  ::fidl::internal::SynchronousProxy proxy_;
};

}  // namespace test
}  // namespace fidl

#endif  // LIB_FIDL_CPP_TEST_FROBINATOR_H_
