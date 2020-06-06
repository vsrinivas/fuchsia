// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/debuglog.h>
#include <lib/unittest/unittest.h>

#include <ktl/unique_ptr.h>

#include "debuglog_internal.h"

namespace {

bool log_format() {
  BEGIN_TEST;

  fbl::AllocChecker ac;
  ktl::unique_ptr<DLog> log = ktl::make_unique<DLog>(&ac);
  ASSERT_TRUE(ac.check(), "");

  char msg[] = "Hello World";

  log->write(DEBUGLOG_WARNING, 0, msg, sizeof(msg));

  dlog_header* header = reinterpret_cast<dlog_header*>(log->data);

  EXPECT_EQ(DEBUGLOG_WARNING, header->severity);
  EXPECT_EQ(0, header->flags);
  EXPECT_EQ(0u, header->pid);
  EXPECT_EQ(0u, header->tid);
  ASSERT_EQ(12, header->datalen);

  uint8_t* msg_bytes = log->data + sizeof(dlog_header);

  EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(&msg[0]), msg_bytes, sizeof(msg));

  END_TEST;
}

bool log_wrap() {
  BEGIN_TEST;

  fbl::AllocChecker ac;
  ktl::unique_ptr<DLog> log = ktl::make_unique<DLog>(&ac);
  ASSERT_TRUE(ac.check(), "");

  char msg[] = "Hello World";
  const size_t kTruncateTarget = 5;  // An unaligned length somewhere in the middle of msg.
  const size_t pad = ALIGN4_TRUNC(DLOG_SIZE - sizeof(dlog_header) - kTruncateTarget);

  // We need to trigger some writes, but we don't care what we write, so we just need a buffer large
  // enough to be copied from. Doesn't much matter what's in it.
  char dummy[DLOG_MAX_DATA]{0};

  for (size_t to_write = pad; to_write;) {
    size_t write = to_write - sizeof(dlog_header);
    write = write > DLOG_MAX_DATA ? DLOG_MAX_DATA : write;

    log->write(DEBUGLOG_WARNING, 0, dummy, write);
    to_write -= write + sizeof(dlog_header);
  }

  EXPECT_EQ(pad, log->head);

  log->write(DEBUGLOG_WARNING, 0, msg, sizeof(msg));

  dlog_header* header = reinterpret_cast<dlog_header*>(log->data + pad);

  EXPECT_EQ(DEBUGLOG_WARNING, header->severity);
  EXPECT_EQ(0, header->flags);
  EXPECT_EQ(0u, header->pid);
  EXPECT_EQ(0u, header->tid);
  ASSERT_EQ(sizeof(msg), header->datalen);

  uint8_t* msg_bytes = log->data + pad + sizeof(dlog_header);

  const size_t kTruncatedSize = DLOG_SIZE - pad - sizeof(dlog_header);

  EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(&msg[0]), msg_bytes, kTruncatedSize);
  EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(&msg[8]), log->data, sizeof(msg) - kTruncatedSize);

  END_TEST;
}

bool log_reader_rollout() {
  BEGIN_TEST;

  fbl::AllocChecker ac;
  ktl::unique_ptr<DLog> log = ktl::make_unique<DLog>(&ac);
  ASSERT_TRUE(ac.check(), "");

  DlogReader reader;
  reader.InitializeForTest(log.get());

  char msg[] = "Hello World";

  log->write(DEBUGLOG_WARNING, 0, msg, sizeof(msg));

  uint8_t read_buf[DLOG_MAX_RECORD];
  size_t got;

  ASSERT_EQ(ZX_OK, reader.Read(0, read_buf, sizeof(read_buf), &got));
  ASSERT_EQ(sizeof(dlog_header) + sizeof(msg), got);

  dlog_header* header = reinterpret_cast<dlog_header*>(read_buf);

  EXPECT_EQ(DEBUGLOG_WARNING, header->severity);

  // Header should be zeroed out.
  EXPECT_EQ(0u, header->header);

  EXPECT_EQ(0, header->flags);
  EXPECT_EQ(0u, header->pid);
  EXPECT_EQ(0u, header->tid);
  ASSERT_EQ(sizeof(msg), header->datalen);

  uint8_t* msg_bytes = &read_buf[sizeof(dlog_header)];

  EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(&msg[0]), msg_bytes, sizeof(msg));

  for (size_t i = 0; i < DLOG_SIZE; i += sizeof(dlog_header) + sizeof(msg)) {
    log->write(DEBUGLOG_WARNING, 0, msg, sizeof(msg));
  }

  ASSERT_EQ(ZX_OK, reader.Read(0, read_buf, sizeof(read_buf), &got));
  ASSERT_EQ(sizeof(dlog_header) + sizeof(msg), got);

  header = reinterpret_cast<dlog_header*>(read_buf);

  EXPECT_EQ(DEBUGLOG_WARNING, header->severity);

  // The amount of data rolled out should be snuck in here.
  EXPECT_EQ(sizeof(dlog_header) + sizeof(msg), header->header);

  EXPECT_EQ(0, header->flags);
  EXPECT_EQ(0u, header->pid);
  EXPECT_EQ(0u, header->tid);
  ASSERT_EQ(sizeof(msg), header->datalen);

  msg_bytes = &read_buf[sizeof(dlog_header)];

  EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(&msg[0]), msg_bytes, sizeof(msg));

  reader.Disconnect();
  END_TEST;
}

}  // namespace

#define DEBUGLOG_UNITTEST(fname) UNITTEST(#fname, fname)

UNITTEST_START_TESTCASE(debuglog_tests)
DEBUGLOG_UNITTEST(log_format)
DEBUGLOG_UNITTEST(log_wrap)
DEBUGLOG_UNITTEST(log_reader_rollout)
UNITTEST_END_TESTCASE(debuglog_tests, "debuglog_tests", "Debuglog test")
