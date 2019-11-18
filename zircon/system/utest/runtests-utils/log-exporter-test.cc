// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/message_buffer.h>
#include <lib/fidl/txn_header.h>

#include <utility>

#include <fbl/string.h>
#include <fbl/vector.h>
#include <runtests-utils/log-exporter.h>
#include <unittest/unittest.h>

namespace runtests {
namespace {

// This class is the helper class which will encapsulate a LogMessage and then
// fill default values for fidl log message object. It also helps in determining
// the length of fidl message required to encode this msg object.
class LogMessage {
 public:
  LogMessage(fbl::String msg, std::initializer_list<fbl::String> tags, uint32_t dropped_logs = 0,
             uint64_t pid = 1024)
      : msg_(msg), tags_(tags), pid_(pid), dropped_logs_(dropped_logs) {}

  LogMessage(fbl::String msg, uint32_t dropped_logs = 0, uint64_t pid = 1024)
      : LogMessage(msg, {}, dropped_logs, pid) {}

  size_t TagsCount() const { return tags_.size(); }

  const fbl::String* tag(size_t index) const { return &tags_[index]; }

  const fbl::String* msg() const { return &msg_; }

  void SetFidlLogMessageValues(fuchsia_logger_LogMessage* lm) const {
    lm->pid = pid_;
    lm->tid = 1034;
    lm->time = 93892493921;
    lm->severity = fuchsia_logger_LogLevelFilter_INFO;
    lm->dropped_logs = dropped_logs_;
    lm->tags.count = tags_.size();
    lm->tags.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
    lm->msg.size = msg_.length();
    lm->msg.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
  }

  size_t GetFidlStringLen() const {
    size_t len = 0;
    for (size_t i = 0; i < tags_.size(); i++) {
      len += FIDL_ALIGN(tags_[i].length());
    }
    return len + FIDL_ALIGN(msg_.length());
  }

 private:
  fbl::String msg_;
  fbl::Vector<fbl::String> tags_;
  uint64_t pid_;
  uint32_t dropped_logs_;
};

// Encode |log_msg| and fills up fidl message object to be sent to LogListener
// implementation.
size_t FillLogMessagePayload(fuchsia_logger_LogMessage* lm, fidl_string_t* strings,
                             const LogMessage& log_msg) {
  log_msg.SetFidlLogMessageValues(lm);
  uint8_t* payload = reinterpret_cast<uint8_t*>(strings + log_msg.TagsCount());
  size_t offset = 0;

  // write tags
  for (size_t i = 0; i < log_msg.TagsCount(); ++i) {
    size_t size = log_msg.tag(i)->length();
    strings[i].size = size;
    strings[i].data = reinterpret_cast<char*> FIDL_ALLOC_PRESENT;
    memcpy(payload + offset, log_msg.tag(i)->data(), size);
    offset += FIDL_ALIGN(size);
  }

  // write msg
  auto msg = log_msg.msg();
  memcpy(payload + offset, msg->data(), msg->length());
  offset += FIDL_ALIGN(msg->length());
  return log_msg.TagsCount() * sizeof(fidl_string_t) + offset;
}

// Encode |log_msgs| with help from other helpers and writes the msg to
// |listener|.
zx_status_t SendLogMessagesHelper(const zx::channel& listener, uint64_t ordinal,
                                  const fbl::Vector<LogMessage>& log_msgs) {
  size_t n_msgs = log_msgs.size();
  if (n_msgs == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  size_t tags_n_msgs = 0;
  size_t len = 0;
  for (size_t i = 0; i < n_msgs; i++) {
    tags_n_msgs += log_msgs[i].TagsCount();
    len += log_msgs[i].GetFidlStringLen();
  }

  size_t msg_len = sizeof(fidl_message_header_t) + n_msgs * sizeof(fuchsia_logger_LogMessage) +
                   tags_n_msgs * sizeof(fidl_string_t) + FIDL_ALIGN(len);
  if (ordinal == fuchsia_logger_LogListenerLogManyOrdinal ||
      ordinal == fuchsia_logger_LogListenerLogManyGenOrdinal) {
    msg_len += sizeof(fidl_vector_t);
  }
  if (msg_len > UINT32_MAX) {
    return ZX_ERR_INVALID_ARGS;
  }
  uint8_t msg[msg_len];
  memset(msg, 0, msg_len);
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg);
  fidl_init_txn_header(hdr, 0, ordinal);
  fuchsia_logger_LogMessage* fidl_log_msg = reinterpret_cast<fuchsia_logger_LogMessage*>(hdr + 1);
  if (ordinal == fuchsia_logger_LogListenerLogManyOrdinal ||
      ordinal == fuchsia_logger_LogListenerLogManyGenOrdinal) {
    fidl_vector_t* vector = reinterpret_cast<fidl_vector_t*>(hdr + 1);
    vector->count = n_msgs;
    vector->data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
    fidl_log_msg = reinterpret_cast<fuchsia_logger_LogMessage*>(vector + 1);
  }
  fidl_string_t* strings = reinterpret_cast<fidl_string_t*>(fidl_log_msg + n_msgs);

  for (size_t i = 0; i < n_msgs; i++) {
    size_t offset = FillLogMessagePayload(fidl_log_msg, strings, log_msgs[i]);
    if (i < n_msgs - 1) {
      fidl_log_msg++;
      strings = reinterpret_cast<fidl_string_t*>((reinterpret_cast<uint8_t*>(strings)) + offset);
    }
  }
  return listener.write(0, msg, static_cast<uint32_t>(msg_len), NULL, 0);
}

// Encodes and writes |log_msg| to |listener|. This will replicate behaviour of
// Log call in Log interface.
zx_status_t SendLogMessage(const zx::channel& listener, LogMessage log_msg) {
  fbl::Vector<LogMessage> log_msgs;
  log_msgs.push_back(std::move(log_msg));
  return SendLogMessagesHelper(listener, fuchsia_logger_LogListenerLogGenOrdinal, log_msgs);
}

// Encodes and writes |log_msgs| to |listener|. This will replicate behaviour of
// LogMany call in Log interface.
zx_status_t SendLogMessages(const zx::channel& listener, const fbl::Vector<LogMessage>& log_msgs) {
  return SendLogMessagesHelper(listener, fuchsia_logger_LogListenerLogManyGenOrdinal, log_msgs);
}

bool TestLog() {
  BEGIN_TEST;
  zx::channel listener, listener_request;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &listener, &listener_request));

  // We expect the log file to be much smaller than this.
  char buf[1024];
  memset(buf, 0, sizeof(buf));
  FILE* buf_file = fmemopen(buf, sizeof(buf), "w");

  // start listener
  auto log_listener = std::make_unique<LogExporter>(std::move(listener_request), buf_file);
  log_listener->set_error_handler([](zx_status_t status) { EXPECT_EQ(ZX_ERR_CANCELED, status); });

  LogMessage msg1("my message");
  ASSERT_EQ(ZX_OK, SendLogMessage(listener, std::move(msg1)));
  LogMessage msg2("my message", {"tag123"});
  ASSERT_EQ(ZX_OK, SendLogMessage(listener, std::move(msg2)));

  ASSERT_EQ(ZX_OK, log_listener->RunUntilIdle());
  fflush(buf_file);

  ASSERT_STR_EQ(R"([00093.892493][1024][1034][] INFO: my message
[00093.892493][1024][1034][tag123] INFO: my message
)",
                buf);

  LogMessage msg3("my message", {"tag123", "tag2"});
  ASSERT_EQ(ZX_OK, SendLogMessage(listener, std::move(msg3)));

  ASSERT_EQ(ZX_OK, log_listener->RunUntilIdle());
  fflush(buf_file);

  ASSERT_STR_EQ(R"([00093.892493][1024][1034][] INFO: my message
[00093.892493][1024][1034][tag123] INFO: my message
[00093.892493][1024][1034][tag123, tag2] INFO: my message
)",
                buf);
  END_TEST;
}

bool TestLogMany() {
  BEGIN_TEST;
  zx::channel listener, listener_request;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &listener, &listener_request));

  // We expect the log file to be much smaller than this.
  char buf[1024];
  memset(buf, 0, sizeof(buf));
  FILE* buf_file = fmemopen(buf, sizeof(buf), "w");

  // start listener
  auto log_listener = std::make_unique<LogExporter>(std::move(listener_request), buf_file);
  log_listener->set_error_handler([](zx_status_t status) { EXPECT_EQ(ZX_ERR_CANCELED, status); });

  LogMessage msg1("my message");
  LogMessage msg2("my message2", {"tag1", "tag2"});
  fbl::Vector<LogMessage> msgs;
  msgs.push_back(std::move(msg1));
  msgs.push_back(std::move(msg2));
  ASSERT_EQ(ZX_OK, SendLogMessages(listener, msgs));

  ASSERT_EQ(ZX_OK, log_listener->RunUntilIdle());
  fflush(buf_file);

  ASSERT_STR_EQ(R"([00093.892493][1024][1034][] INFO: my message
[00093.892493][1024][1034][tag1, tag2] INFO: my message2
)",
                buf);
  LogMessage msg3("my message", {"tag1"});
  msgs.reset();
  msgs.push_back(std::move(msg3));
  ASSERT_EQ(ZX_OK, SendLogMessages(listener, msgs));

  ASSERT_EQ(ZX_OK, log_listener->RunUntilIdle());
  fflush(buf_file);

  ASSERT_STR_EQ(R"([00093.892493][1024][1034][] INFO: my message
[00093.892493][1024][1034][tag1, tag2] INFO: my message2
[00093.892493][1024][1034][tag1] INFO: my message
)",
                buf);
  END_TEST;
}

bool TestDroppedLogs() {
  BEGIN_TEST;
  zx::channel listener, listener_request;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &listener, &listener_request));

  // We expect the log file to be much smaller than this.
  char buf[1024];
  memset(buf, 0, sizeof(buf));
  FILE* buf_file = fmemopen(buf, sizeof(buf), "w");

  // start listener
  auto log_listener = std::make_unique<LogExporter>(std::move(listener_request), buf_file);
  log_listener->set_error_handler([](zx_status_t status) { EXPECT_EQ(ZX_ERR_CANCELED, status); });

  LogMessage msg1("my message1", 1);
  ASSERT_EQ(ZX_OK, SendLogMessage(listener, std::move(msg1)));
  LogMessage msg2("my message2", 1);
  ASSERT_EQ(ZX_OK, SendLogMessage(listener, std::move(msg2)));
  LogMessage msg3("my message3", 1, 1011);
  ASSERT_EQ(ZX_OK, SendLogMessage(listener, std::move(msg3)));
  LogMessage msg4("my message4", 1, 1011);
  ASSERT_EQ(ZX_OK, SendLogMessage(listener, std::move(msg4)));
  LogMessage msg5("my message5", 2, 1011);
  ASSERT_EQ(ZX_OK, SendLogMessage(listener, std::move(msg5)));
  LogMessage msg6("my message6", 2);
  ASSERT_EQ(ZX_OK, SendLogMessage(listener, std::move(msg6)));

  ASSERT_EQ(ZX_OK, log_listener->RunUntilIdle());
  fflush(buf_file);

  ASSERT_STR_EQ(R"([00093.892493][1024][1034][] INFO: my message1
[00093.892493][1024][1034][] WARNING: Dropped logs count:1
[00093.892493][1024][1034][] INFO: my message2
[00093.892493][1011][1034][] INFO: my message3
[00093.892493][1011][1034][] WARNING: Dropped logs count:1
[00093.892493][1011][1034][] INFO: my message4
[00093.892493][1011][1034][] INFO: my message5
[00093.892493][1011][1034][] WARNING: Dropped logs count:2
[00093.892493][1024][1034][] INFO: my message6
[00093.892493][1024][1034][] WARNING: Dropped logs count:2
)",
                buf);

  END_TEST;
}

bool TestBadOutputFile() {
  BEGIN_TEST;
  zx::channel listener, listener_request;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &listener, &listener_request));

  char buf[1024];
  memset(buf, 0, sizeof(buf));
  FILE* buf_file = fmemopen(buf, sizeof(buf), "r");

  // start listener
  auto log_listener = std::make_unique<LogExporter>(std::move(listener_request), buf_file);
  log_listener->set_error_handler(
      [](zx_status_t status) { EXPECT_EQ(ZX_ERR_ACCESS_DENIED, status); });

  LogMessage msg1("my message");
  ASSERT_EQ(ZX_OK, SendLogMessage(listener, std::move(msg1)));

  ASSERT_EQ(ZX_OK, log_listener->RunUntilIdle());
  ASSERT_STR_EQ("", buf);

  END_TEST;
}

BEGIN_TEST_CASE(LogListenerTests)
RUN_TEST(TestLog)
RUN_TEST(TestLogMany)
RUN_TEST(TestDroppedLogs)
RUN_TEST(TestBadOutputFile)
END_TEST_CASE(LogListenerTests)

}  // namespace
}  // namespace runtests
