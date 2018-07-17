// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_FAKE_SIGNALING_CHANNEL_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_FAKE_SIGNALING_CHANNEL_H_

#include <lib/async/cpp/task.h>
#include <unordered_map>
#include <vector>

#include "garnet/drivers/bluetooth/lib/l2cap/signaling_channel.h"

namespace btlib {
namespace l2cap {
namespace internal {
namespace testing {

// Can be injected into internal L2CAP tests to drive fake interactions over the
// signaling channels with remote peers (in both directions). Expectations for
// inbound and outbound expected transactions are not synchronized.
class FakeSignalingChannel : public SignalingChannelInterface {
 public:
  // |dispatcher| is the test message loop's dispatcher
  explicit FakeSignalingChannel(async_dispatcher_t* dispatcher);
  ~FakeSignalingChannel() override = default;

  // SignalingChannelInterface overrides
  bool SendRequest(CommandCode req_code, const common::ByteBuffer& payload,
                   ResponseHandler cb) override;
  void ServeRequest(CommandCode req_code, RequestDelegate cb) override;

  // Add an expected outbound request, which FakeSignalingChannel will respond
  // to with a series of responses. The request's contents will be expected to
  // match |req_code| and |req_payload|. The corresponding request handler will
  // be expected to handle as many responses as are provided here for testing.
  // |responses| should be a comma-delimited list of
  // std::pair<Status rsp_status, common::BufferView rsp_payload>.
  template <typename... Responses>
  void AddOutbound(CommandCode req_code, common::BufferView req_payload,
                   Responses... responses) {
    transactions_.push_back(Transaction{req_code, req_payload, {responses...}});
  }

  // Simulate reception of an inbound request with |req_code| and |req_payload|,
  // then expect a corresponding outbound response with payload |rsp_payload|.
  void ReceiveExpect(CommandCode req_code,
                     const common::ByteBuffer& req_payload,
                     const common::ByteBuffer& rsp_payload);

  // Simulate reception of an inbound request with |req_code| and |req_payload|,
  // then expect a matching rejection with the Not Understood reason.
  void ReceiveExpectRejectNotUnderstood(CommandCode req_code,
                                        const common::ByteBuffer& req_payload);

  // Simulate reception of an inbound request with |req_code| and |req_payload|,
  // then expect a matching rejection with the Invalid Channel ID reason and the
  // rejected IDs |local_cid| and |remote_cid|.
  void ReceiveExpectRejectInvalidChannelId(
      CommandCode req_code, const common::ByteBuffer& req_payload,
      ChannelId local_cid, ChannelId remote_cid);

 private:
  // Expected outbound request and response(s) that this fake send(s) back
  struct Transaction {
    CommandCode request_code;
    common::BufferView req_payload;
    std::vector<std::pair<Status, common::BufferView>> responses;
  };

  // Test a previously-registered request handler by simulating an inbound
  // request of |req_code| and |req_payload|. The test will assert-fail if no
  // handler had been registered with |ServeRequest|. |fake_responder| will be
  // generated internally based on the kind of reply that the handler is
  // expected to send and is passed to the handler-under-test. The test will
  // fail if no reply at all is sent.
  void ReceiveExpectInternal(CommandCode req_code,
                             const common::ByteBuffer& req_payload,
                             Responder* fake_responder);

  // Test message loop dispatcher
  async_dispatcher_t* const dispatcher_;

  // Expected outbound transactions
  std::vector<Transaction> transactions_;

  // Index of current outbound transaction expected through SendRequest
  size_t expected_transaction_index_ = 0UL;

  // Registered inbound request delegates
  std::unordered_map<CommandCode, RequestDelegate> request_handlers_;
};

}  // namespace testing
}  // namespace internal
}  // namespace l2cap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_FAKE_SIGNALING_CHANNEL_H_
