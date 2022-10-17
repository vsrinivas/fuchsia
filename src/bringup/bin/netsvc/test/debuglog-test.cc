// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/debuglog.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/status.h>

#include <zxtest/zxtest.h>

constexpr char kNodename[] = "test";

const char* nodename() { return kNodename; }

#define EXPECT_STRINGVIEW_EQ(got, want)                                                  \
  {                                                                                      \
    std::string_view a(got);                                                             \
    std::string_view b(want);                                                            \
    if (a != b) {                                                                        \
      ADD_FAILURE("got: \"%.*s\", want: \"%.*s\"", static_cast<int>(a.size()), a.data(), \
                  static_cast<int>(b.size()), b.data());                                 \
    }                                                                                    \
  }

class LogListenerTestHelper {
 public:
  static void DrainPendingMessages(LogListener& listener) {
    if (listener.pkt_.has_value()) {
      LogListener::PendingMessage& msg = listener.pkt_.value().message;
      ADD_FAILURE("didn't complete staged message \"%s\"", msg.log_message.c_str());
      msg.Complete();
    }
    while (!listener.pending_.empty()) {
      LogListener::PendingMessage& msg = listener.pending_.front();
      ADD_FAILURE("didn't complete pending message \"%s\"", msg.log_message.c_str());
      msg.Complete();
      listener.pending_.pop();
    }
  }

  static void Unbind(LogListener& listener) { listener.binding_.value().Unbind(); }
};

class LogListenerTest : public zxtest::Test {
 public:
  static constexpr int64_t kPid = 6789;
  static constexpr int64_t kTid = 1234;
  static constexpr int64_t kTimeSecs = 987;
  static constexpr int64_t kTimeMillis = 654;
  static constexpr char kTag1[] = "t1";
  static constexpr char kTag2[] = "t2";
  static constexpr size_t kMaxLogData = 64;
  LogListenerTest()
      : loop_(&kAsyncLoopConfigNeverAttachToThread),
        listener_(loop_.dispatcher(), fit::bind_member<&LogListenerTest::OnData>(this),
                  /* retransmit */ false, kMaxLogData) {}

  void SetUp() override {
    zx::result endpoints = fidl::CreateEndpoints<fuchsia_logger::LogListenerSafe>();
    ASSERT_OK(endpoints.status_value());
    auto [client, server] = std::move(endpoints.value());
    listener_.Bind(std::move(server));
    client_.Bind(std::move(client), loop_.dispatcher());

    int size = sprintf(message_preamble_, "[%05ld.%03ld] %05ld.%05ld [%s,%s] ", kTimeSecs,
                       kTimeMillis, kPid, kTid, kTag1, kTag2);
    preamble_view_ = std::string_view(message_preamble_, size);
  }

  void TearDown() override {
    // Drain pending messages to ensure tests drive all messages and ACKs to
    // completion. Otherwise the unfulfilled FIDL completion handlers trip
    // assertions on the destructor.
    LogListenerTestHelper::DrainPendingMessages(listener_);
    // Unbind cleanly to prevent log messages on dispatcher cancel.
    LogListenerTestHelper::Unbind(listener_);
  }

  void OnData(const logpacket_t& pkt, size_t len) {
    EXPECT_EQ(pkt.magic, NB_DEBUGLOG_MAGIC);
    EXPECT_EQ(pkt.seqno, seqno_++);
    EXPECT_STREQ(pkt.nodename, nodename());
    ASSERT_FALSE(log_message_.has_value());
    log_message_ = std::string(pkt.data, len + sizeof(pkt.data) - sizeof(pkt));
    ASSERT_FALSE(needs_ack_.has_value());
    needs_ack_ = pkt.seqno;
  }

  std::optional<std::string> TakeMessage() {
    std::optional<std::string> ret;
    std::swap(ret, log_message_);
    return ret;
  }

  std::string_view SplitAndCheckPreamble(std::string_view message) {
    std::string_view preamble = message.substr(0, preamble_view_.size());
    EXPECT_STRINGVIEW_EQ(preamble, preamble_view_);
    return message.size() >= preamble_view_.size() ? message.substr(preamble_view_.size()) : "";
  }

  std::string_view SplitAndCheckNewline(std::string_view message) {
    if (message.empty()) {
      return message;
    }
    EXPECT_EQ(message.at(message.size() - 1), '\n', "expected newline in %.*s",
              static_cast<int>(message.size()), message.data());
    return message.substr(0, message.size() - 1);
  }

  zx_status_t Log(std::string_view message, std::optional<zx_status_t>& result) {
    fidl::StringView tags[] = {kTag1, kTag2};
    client_
        ->Log(fuchsia_logger::wire::LogMessage{
            .pid = kPid,
            .tid = kTid,
            .time = (zx::sec(kTimeSecs) + zx::msec(kTimeMillis)).to_nsecs(),
            .severity = static_cast<int32_t>(fuchsia_logger::wire::kLogLevelDefault),
            .tags =
                fidl::VectorView<fidl::StringView>::FromExternal(std::begin(tags), std::size(tags)),
            .msg = fidl::StringView::FromExternal(message),
        })
        .ThenExactlyOnce(
            [&result](fidl::WireUnownedResult<fuchsia_logger::LogListenerSafe::Log>& r) {
              result = r.status();
            });
    return loop_.RunUntilIdle();
  }

  zx_status_t LogMany(cpp20::span<const std::string_view> messages,
                      std::optional<zx_status_t>& result) {
    fidl::StringView tags[] = {kTag1, kTag2};
    std::vector<fuchsia_logger::wire::LogMessage> message_vec;
    message_vec.reserve(messages.size());
    for (const std::string_view& msg : messages) {
      message_vec.push_back(fuchsia_logger::wire::LogMessage{
          .pid = kPid,
          .tid = kTid,
          .time = (zx::sec(kTimeSecs) + zx::msec(kTimeMillis)).to_nsecs(),
          .severity = static_cast<int32_t>(fuchsia_logger::wire::kLogLevelDefault),
          .tags =
              fidl::VectorView<fidl::StringView>::FromExternal(std::begin(tags), std::size(tags)),
          .msg = fidl::StringView::FromExternal(msg),
      });
    }
    client_->LogMany(fidl::VectorView<fuchsia_logger::wire::LogMessage>::FromExternal(message_vec))
        .ThenExactlyOnce(
            [&result](fidl::WireUnownedResult<fuchsia_logger::LogListenerSafe::LogMany>& r) {
              result = r.status();
            });
    return loop_.RunUntilIdle();
  }

  zx_status_t Ack() {
    std::optional<uint32_t> ack;
    std::swap(ack, needs_ack_);
    if (!ack.has_value()) {
      ADD_FAILURE("no message needs ACK");
      return ZX_ERR_BAD_STATE;
    }
    listener_.Ack(ack.value());
    return loop_.RunUntilIdle();
  }

  zx_status_t LogAndAck(std::string_view message) {
    std::optional<zx_status_t> result;

    if (zx_status_t status = Log(message, result); status != ZX_OK) {
      return status;
    }
    if (zx_status_t status = Ack(); status != ZX_OK) {
      return status;
    }

    if (!result.has_value()) {
      ADD_FAILURE("failed to respond to log request");
      return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
  }

  std::string_view preamble() { return preamble_view_; }

  void FillTestBuffer(cpp20::span<char> chars) {
    for (size_t i = 0; i < chars.size(); i++) {
      chars[i] = 'a' + static_cast<char>(i % ('z' - 'a'));
    }
  }

 private:
  char message_preamble_[128];
  std::string_view preamble_view_;
  async::Loop loop_;
  LogListener listener_;
  uint32_t seqno_ = 1;
  std::optional<uint32_t> needs_ack_;
  std::optional<std::string> log_message_;
  fidl::WireClient<fuchsia_logger::LogListenerSafe> client_;
};

TEST_F(LogListenerTest, LogOnce) {
  constexpr char kMsg[] = "hello";
  ASSERT_OK(LogAndAck(kMsg));
  std::optional msg = TakeMessage();
  ASSERT_TRUE(msg.has_value());
  EXPECT_STRINGVIEW_EQ(SplitAndCheckNewline(SplitAndCheckPreamble(msg.value())), kMsg);
}

TEST_F(LogListenerTest, LogMany) {
  constexpr std::string_view kMessages[] = {"hello", "there", "world"};
  std::optional<zx_status_t> result;
  ASSERT_OK(LogMany(cpp20::span(std::begin(kMessages), std::size(kMessages)), result));
  for (const std::string_view& expect : kMessages) {
    ASSERT_FALSE(result.has_value());
    std::optional msg = TakeMessage();
    ASSERT_TRUE(msg.has_value());
    EXPECT_STRINGVIEW_EQ(SplitAndCheckNewline(SplitAndCheckPreamble(msg.value())), expect);
    ASSERT_OK(Ack());
  }
  ASSERT_TRUE(result.has_value());
  ASSERT_OK(result.value());
}

TEST_F(LogListenerTest, EnqueueLogs) {
  constexpr std::string_view kMessages[] = {"hello", "there", "world"};
  std::array<std::optional<zx_status_t>, std::size(kMessages)> rsp;
  for (size_t i = 0; i < std::size(kMessages); i++) {
    ASSERT_OK(Log(kMessages[i], rsp[i]));
  }
  for (size_t i = 0; i < std::size(kMessages); i++) {
    const std::string_view& expect = kMessages[i];
    std::optional msg = TakeMessage();
    ASSERT_TRUE(msg.has_value());
    EXPECT_STRINGVIEW_EQ(SplitAndCheckNewline(SplitAndCheckPreamble(msg.value())), expect);
    ASSERT_OK(Ack());
    ASSERT_TRUE(rsp[i].has_value());
    ASSERT_OK(rsp[i].value());
  }
}

TEST_F(LogListenerTest, SplitMessage) {
  char large_message[2 * kMaxLogData + kMaxLogData / 4];
  FillTestBuffer(large_message);
  std::optional<zx_status_t> result;
  std::string_view full_msg(large_message, std::size(large_message));
  ASSERT_OK(Log(full_msg, result));
  // Should be split in three messages.
  {
    std::optional msg = TakeMessage();
    ASSERT_TRUE(msg.has_value());
    // First message contains the preamble and no newline.
    EXPECT_STRINGVIEW_EQ(SplitAndCheckPreamble(msg.value()),
                         full_msg.substr(0, kMaxLogData - preamble().size()));
    ASSERT_OK(Ack());
    ASSERT_FALSE(result.has_value());
  }
  {
    std::optional msg = TakeMessage();
    ASSERT_TRUE(msg.has_value());
    // Second message contains only data.
    EXPECT_STRINGVIEW_EQ(msg.value(),
                         full_msg.substr(kMaxLogData - preamble().size(), kMaxLogData));
    ASSERT_OK(Ack());
    ASSERT_FALSE(result.has_value());
  }
  {
    std::optional msg = TakeMessage();
    ASSERT_TRUE(msg.has_value());
    // Third message adds a newline.
    EXPECT_STRINGVIEW_EQ(SplitAndCheckNewline(msg.value()),
                         full_msg.substr(2 * kMaxLogData - preamble().size()));
    ASSERT_OK(Ack());
    ASSERT_TRUE(result.has_value());
    ASSERT_OK(result.value());
  }
}

TEST_F(LogListenerTest, JustFits) {
  char large_message[kMaxLogData];
  FillTestBuffer(large_message);
  std::optional<zx_status_t> result;
  std::string_view full_msg(large_message, kMaxLogData - preamble().size() - 1);
  ASSERT_OK(Log(full_msg, result));
  std::optional msg = TakeMessage();
  ASSERT_TRUE(msg.has_value());
  EXPECT_STRINGVIEW_EQ(SplitAndCheckNewline(SplitAndCheckPreamble(msg.value())), full_msg);
  ASSERT_OK(Ack());
  ASSERT_TRUE(result.has_value());
  ASSERT_OK(result.value());
}

TEST_F(LogListenerTest, SplitsNewline) {
  char large_message[kMaxLogData];
  FillTestBuffer(large_message);
  std::optional<zx_status_t> result;
  std::string_view full_msg(large_message, kMaxLogData - preamble().size());
  ASSERT_OK(Log(full_msg, result));

  {
    std::optional msg = TakeMessage();
    ASSERT_TRUE(msg.has_value());
    EXPECT_STRINGVIEW_EQ(SplitAndCheckPreamble(msg.value()), full_msg);
    ASSERT_OK(Ack());
    ASSERT_FALSE(result.has_value());
  }
  {
    std::optional msg = TakeMessage();
    ASSERT_TRUE(msg.has_value());
    EXPECT_STRINGVIEW_EQ(msg.value(), "\n");
    ASSERT_OK(Ack());
    ASSERT_TRUE(result.has_value());
    ASSERT_OK(result.value());
  }
}

TEST_F(LogListenerTest, JustFitsOnSecondMessage) {
  char large_message[2 * kMaxLogData];
  FillTestBuffer(large_message);
  std::optional<zx_status_t> result;
  std::string_view full_msg(large_message, 2 * kMaxLogData - preamble().size() - 1);
  ASSERT_OK(Log(full_msg, result));
  {
    std::optional msg = TakeMessage();
    ASSERT_TRUE(msg.has_value());
    // First message contains the preamble and no newline.
    EXPECT_STRINGVIEW_EQ(SplitAndCheckPreamble(msg.value()),
                         full_msg.substr(0, kMaxLogData - preamble().size()));
    ASSERT_OK(Ack());
    ASSERT_FALSE(result.has_value());
  }
  {
    std::optional msg = TakeMessage();
    ASSERT_TRUE(msg.has_value());
    // SecondMessage contains rest and newline.
    EXPECT_STRINGVIEW_EQ(SplitAndCheckNewline(msg.value()),
                         full_msg.substr(kMaxLogData - preamble().size()));
    ASSERT_OK(Ack());
    ASSERT_TRUE(result.has_value());
    ASSERT_OK(result.value());
  }
}

TEST_F(LogListenerTest, SplitsNewlineToThirdMessage) {
  char large_message[2 * kMaxLogData];
  FillTestBuffer(large_message);
  std::optional<zx_status_t> result;
  std::string_view full_msg(large_message, 2 * kMaxLogData - preamble().size());
  ASSERT_OK(Log(full_msg, result));
  {
    std::optional msg = TakeMessage();
    ASSERT_TRUE(msg.has_value());
    // First message contains the preamble and no newline.
    EXPECT_STRINGVIEW_EQ(SplitAndCheckPreamble(msg.value()),
                         full_msg.substr(0, kMaxLogData - preamble().size()));
    ASSERT_OK(Ack());
    ASSERT_FALSE(result.has_value());
  }
  {
    std::optional msg = TakeMessage();
    ASSERT_TRUE(msg.has_value());
    // SecondMessage contains rest.
    EXPECT_STRINGVIEW_EQ(msg.value(), full_msg.substr(kMaxLogData - preamble().size()));
    ASSERT_OK(Ack());
    ASSERT_FALSE(result.has_value());
  }
  {
    std::optional msg = TakeMessage();
    ASSERT_TRUE(msg.has_value());
    // Final message contains newline.
    EXPECT_STRINGVIEW_EQ(msg.value(), "\n");
    ASSERT_OK(Ack());
    ASSERT_TRUE(result.has_value());
    ASSERT_OK(result.value());
  }
}
