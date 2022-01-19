// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_MOCK_CLIENT_IMPL_H_
#define SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_MOCK_CLIENT_IMPL_H_

#include <lib/fidl/llcpp/client.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/wire_messaging.h>

#include <unordered_set>

#include <zxtest/zxtest.h>

#include "client_checkers.h"

namespace fidl_testing {

class TestProtocol {
 public:
  using Transport = fidl::internal::ChannelTransport;
  TestProtocol() = delete;
};

// |ClientBaseSpy| delegates calls to |ClientBase| but in addition records
// extra information about the transactions which are useful for unit testing.
class ClientBaseSpy {
 public:
  // In cases the spy needs bound client, but the client also needs a spy,
  // construct an empty |ClientBaseSpy| first, then call |set_client|.
  ClientBaseSpy() : client_base_(nullptr) {}

  explicit ClientBaseSpy(fidl::internal::ClientBase* client_base) : client_base_(client_base) {
    ZX_ASSERT(client_base != nullptr);
  }

  template <typename ClientLike>
  explicit ClientBaseSpy(ClientLike&& client)
      : ClientBaseSpy(ClientChecker::GetClientBase(client)) {}

  template <typename ClientLike>
  void set_client(ClientLike&& client) {
    client_base_ = ClientChecker::GetClientBase(client);
  }

  void PrepareAsyncTxn(fidl::internal::ResponseContext* context) {
    client_base_->PrepareAsyncTxn(context);
    std::unique_lock lock(lock_);
    EXPECT_FALSE(txids_.count(context->Txid()));
    txids_.insert(context->Txid());
  }

  void ForgetAsyncTxn(fidl::internal::ResponseContext* context) {
    {
      std::unique_lock lock(lock_);
      txids_.erase(context->Txid());
    }
    client_base_->ForgetAsyncTxn(context);
  }

  void EraseTxid(fidl::internal::ResponseContext* context) {
    {
      std::unique_lock lock(lock_);
      txids_.erase(context->Txid());
    }
  }

  template <typename Callable>
  auto MakeSyncCallWith(Callable&& sync_call) {
    return client_base_->MakeSyncCallWith(std::forward<Callable>(sync_call));
  }

  bool IsPending(zx_txid_t txid) {
    std::unique_lock lock(lock_);
    return txids_.count(txid);
  }

  size_t GetTxidCount() {
    std::unique_lock lock(lock_);
    EXPECT_EQ(client_base_->GetTransactionCount(), txids_.size());
    return txids_.size();
  }

 private:
  fidl::internal::ClientBase* client_base_;
  std::mutex lock_;
  std::unordered_set<zx_txid_t> txids_;
};

}  // namespace fidl_testing

template <>
class ::fidl::WireAsyncEventHandler<fidl_testing::TestProtocol>
    : public fidl::internal::AsyncEventHandler {
 public:
  WireAsyncEventHandler() = default;
  ~WireAsyncEventHandler() override = default;

  void on_fidl_error(::fidl::UnbindInfo info) override {}

  void LogEvent() { event_count_++; }

  uint32_t event_count() const { return event_count_; }

 private:
  uint32_t event_count_ = 0;
};

template <>
class ::fidl::internal::WireEventDispatcher<fidl_testing::TestProtocol>
    : public ::fidl::internal::IncomingEventDispatcher<
          ::fidl::WireAsyncEventHandler<fidl_testing::TestProtocol>> {
 public:
  explicit WireEventDispatcher(
      ::fidl::WireAsyncEventHandler<fidl_testing::TestProtocol>* event_handler)
      : IncomingEventDispatcher(event_handler) {}

 private:
  // For each event, increment the event count.
  std::optional<UnbindInfo> DispatchEvent(
      fidl::IncomingMessage& msg, internal::IncomingTransportContext transport_context) override {
    event_handler()->LogEvent();
    return {};
  }
};

template <>
class ::fidl::internal::WireWeakAsyncClientImpl<fidl_testing::TestProtocol>
    : public fidl::internal::ClientImplBase {
 public:
  using ClientImplBase::ClientImplBase;
};

template <>
class ::fidl::internal::WireWeakOnewayBufferClientImpl<fidl_testing::TestProtocol>
    : public ::fidl::internal::BufferClientImplBase {
 public:
  using BufferClientImplBase::BufferClientImplBase;
};

template <>
class ::fidl::internal::WireWeakAsyncBufferClientImpl<fidl_testing::TestProtocol>
    : public ::fidl::internal::WireWeakOnewayBufferClientImpl<fidl_testing::TestProtocol> {
 public:
  using WireWeakOnewayBufferClientImpl::WireWeakOnewayBufferClientImpl;
};

template <>
class ::fidl::internal::WireWeakOnewayClientImpl<fidl_testing::TestProtocol>
    : public ::fidl::internal::ClientImplBase {
 public:
  using ClientImplBase::ClientImplBase;
};

template <>
class ::fidl::internal::WireWeakSyncClientImpl<fidl_testing::TestProtocol>
    : public ::fidl::internal::WireWeakOnewayClientImpl<fidl_testing::TestProtocol> {
 public:
  using WireWeakOnewayClientImpl::WireWeakOnewayClientImpl;
};

namespace fidl_testing {

class TestResponseContext : public fidl::internal::ResponseContext {
 public:
  explicit TestResponseContext(ClientBaseSpy* spy)
      : fidl::internal::ResponseContext(0), spy_(spy) {}
  cpp17::optional<fidl::UnbindInfo> OnRawResult(
      fidl::IncomingMessage&& msg,
      fidl::internal::IncomingTransportContext transport_context) override {
    spy_->EraseTxid(this);
    return cpp17::nullopt;
  }

 private:
  ClientBaseSpy* spy_;
};

}  // namespace fidl_testing

#endif  // SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_MOCK_CLIENT_IMPL_H_
