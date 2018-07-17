// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/l2cap/fake_signaling_channel.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "lib/gtest/test_loop_fixture.h"

namespace btlib {
namespace l2cap {
namespace internal {
namespace testing {
namespace {

// These classes bind the response that the request handlers are expected to
// send back. These also serve as the actual Responder implementation that the
// request handler under test will see. These roles may need to be decoupled if
// request handlers have to be tested for multiple responses to each request.
class Expecter : public SignalingChannel::Responder {
 public:
  void Send(const common::ByteBuffer& rsp_payload) override {
    FAIL() << "Unexpected local response " << rsp_payload.AsString();
  }

  void RejectNotUnderstood() override {
    FAIL() << "Unexpected local rejection, \"Not Understood\"";
  }

  void RejectInvalidChannelId(ChannelId local_cid,
                              ChannelId remote_cid) override {
    FAIL() << fxl::StringPrintf(
        "Unexpected local rejection, \"Invalid Channel ID\" local: %#06x "
        "remote: %#06x",
        local_cid, remote_cid);
  }

  bool called() const { return called_; }

 protected:
  void set_called(bool called) { called_ = called; }

 private:
  bool called_ = false;
};

class ResponseExpecter : public Expecter {
 public:
  explicit ResponseExpecter(const common::ByteBuffer& expected_rsp)
      : expected_rsp_(expected_rsp) {}

  void Send(const common::ByteBuffer& rsp_payload) override {
    set_called(true);
    EXPECT_TRUE(common::ContainersEqual(expected_rsp_, rsp_payload));
  }

 private:
  const common::ByteBuffer& expected_rsp_;
};

class RejectNotUnderstoodExpecter : public Expecter {
 public:
  void RejectNotUnderstood() override { set_called(true); }
};

class RejectInvalidChannelIdExpecter : public Expecter {
 public:
  RejectInvalidChannelIdExpecter(ChannelId local_cid, ChannelId remote_cid)
      : local_cid_(local_cid), remote_cid_(remote_cid) {}

  void RejectInvalidChannelId(ChannelId local_cid,
                              ChannelId remote_cid) override {
    set_called(true);
    EXPECT_EQ(local_cid_, local_cid);
    EXPECT_EQ(remote_cid_, remote_cid);
  }

 private:
  const ChannelId local_cid_;
  const ChannelId remote_cid_;
};

}  // namespace

FakeSignalingChannel::FakeSignalingChannel(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {
  FXL_DCHECK(dispatcher_);
}

bool FakeSignalingChannel::SendRequest(CommandCode req_code,
                                       const common::ByteBuffer& payload,
                                       SignalingChannel::ResponseHandler cb) {
  if (expected_transaction_index_ >= transactions_.size()) {
    return false;
  }

  const Transaction& transaction = transactions_[expected_transaction_index_];
  EXPECT_EQ(transaction.request_code, req_code);
  EXPECT_TRUE(common::ContainersEqual(transaction.req_payload, payload));

  // Simulate the remote's response(s)
  async::PostTask(dispatcher_, [this, cb = std::move(cb),
                                index = expected_transaction_index_]() {
    size_t responses_handled = 0;
    const Transaction& transaction = transactions_[index];
    for (auto& response : transaction.responses) {
      responses_handled++;
      if (!cb(response.first, response.second)) {
        break;
      }
    }
    ASSERT_EQ(transaction.responses.size(), responses_handled);
  });

  expected_transaction_index_++;
  return (transaction.request_code == req_code);
}

void FakeSignalingChannel::ServeRequest(CommandCode req_code,
                                        SignalingChannel::RequestDelegate cb) {
  request_handlers_[req_code] = std::move(cb);
}

void FakeSignalingChannel::ReceiveExpect(
    CommandCode req_code, const common::ByteBuffer& req_payload,
    const common::ByteBuffer& rsp_payload) {
  ResponseExpecter expecter(rsp_payload);
  ReceiveExpectInternal(req_code, req_payload, &expecter);
}

void FakeSignalingChannel::ReceiveExpectRejectNotUnderstood(
    CommandCode req_code, const common::ByteBuffer& req_payload) {
  RejectNotUnderstoodExpecter expecter;
  ReceiveExpectInternal(req_code, req_payload, &expecter);
}

void FakeSignalingChannel::ReceiveExpectRejectInvalidChannelId(
    CommandCode req_code, const common::ByteBuffer& req_payload,
    ChannelId local_cid, ChannelId remote_cid) {
  RejectInvalidChannelIdExpecter expecter(local_cid, remote_cid);
  ReceiveExpectInternal(req_code, req_payload, &expecter);
}

// Test evaluator for inbound requests with type-erased, bound expected requests
void FakeSignalingChannel::ReceiveExpectInternal(
    CommandCode req_code, const common::ByteBuffer& req_payload,
    Responder* fake_responder) {
  auto iter = request_handlers_.find(req_code);
  ASSERT_NE(request_handlers_.end(), iter);

  // Invoke delegate assigned for this request type
  iter->second(req_payload, fake_responder);
  EXPECT_TRUE(static_cast<Expecter*>(fake_responder)->called());
}

}  // namespace testing
}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
