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

namespace fidl_testing {

class TestProtocol {
  TestProtocol() = delete;
};

}  // namespace fidl_testing

template <>
class ::fidl::WireAsyncEventHandler<fidl_testing::TestProtocol>
    : public fidl::internal::AsyncEventHandler {
 public:
  WireAsyncEventHandler() = default;
  ~WireAsyncEventHandler() override = default;

  void on_fidl_error(::fidl::UnbindInfo info) override {}
};

template <>
class ::fidl::internal::WireClientImpl<fidl_testing::TestProtocol>
    : public fidl::internal::ClientBase {
 public:
  void PrepareAsyncTxn(internal::ResponseContext* context) {
    internal::ClientBase::PrepareAsyncTxn(context);
    std::unique_lock lock(lock_);
    EXPECT_FALSE(txids_.count(context->Txid()));
    txids_.insert(context->Txid());
  }

  void ForgetAsyncTxn(internal::ResponseContext* context) {
    {
      std::unique_lock lock(lock_);
      txids_.erase(context->Txid());
    }
    internal::ClientBase::ForgetAsyncTxn(context);
  }

  void EraseTxid(internal::ResponseContext* context) {
    {
      std::unique_lock lock(lock_);
      txids_.erase(context->Txid());
    }
  }

  std::shared_ptr<internal::ChannelRef> GetChannel() { return internal::ClientBase::GetChannel(); }

  uint32_t GetEventCount() {
    std::unique_lock lock(lock_);
    return event_count_;
  }

  bool IsPending(zx_txid_t txid) {
    std::unique_lock lock(lock_);
    return txids_.count(txid);
  }

  size_t GetTxidCount() {
    std::unique_lock lock(lock_);
    EXPECT_EQ(internal::ClientBase::GetTransactionCount(), txids_.size());
    return txids_.size();
  }

  WireClientImpl() = default;

 private:
  // For each event, increment the event count.
  std::optional<UnbindInfo> DispatchEvent(fidl::IncomingMessage& msg,
                                          AsyncEventHandler* event_handler) override {
    event_count_++;
    return {};
  }

  std::mutex lock_;
  std::unordered_set<zx_txid_t> txids_;
  uint32_t event_count_ = 0;
};

namespace fidl_testing {

class TestResponseContext : public fidl::internal::ResponseContext {
 public:
  explicit TestResponseContext(fidl::internal::WireClientImpl<TestProtocol>* client)
      : fidl::internal::ResponseContext(0), client_(client) {}
  cpp17::optional<fidl::UnbindInfo> OnRawResult(fidl::IncomingMessage&& msg) override {
    client_->EraseTxid(this);
    return cpp17::nullopt;
  }
  void OnCanceled() override {}

 private:
  fidl::internal::WireClientImpl<TestProtocol>* client_;
};

}  // namespace fidl_testing

#endif  // SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_MOCK_CLIENT_IMPL_H_
