// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/debuglog.h>
#include <lib/unittest/unittest.h>
#include <zircon/types.h>

#include <ktl/unique_ptr.h>

#include "debuglog_internal.h"

struct DebuglogTests {
  static bool log_format() {
    BEGIN_TEST;

    fbl::AllocChecker ac;
    ktl::unique_ptr<DLog> log = ktl::make_unique<DLog>(&ac);
    ASSERT_TRUE(ac.check(), "");

    char msg[] = "Hello World";

    log->write(DEBUGLOG_WARNING, 0, {msg, sizeof(msg)});

    dlog_header* header = reinterpret_cast<dlog_header*>(log->data_);

    EXPECT_EQ(DEBUGLOG_WARNING, header->severity);
    EXPECT_EQ(0, header->flags);
    EXPECT_EQ(ZX_KOID_INVALID, header->pid);
    EXPECT_NE(ZX_KOID_INVALID, header->tid);
    ASSERT_EQ(12, header->datalen);

    uint8_t* msg_bytes = log->data_ + sizeof(dlog_header);

    EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(msg), msg_bytes, sizeof(msg));

    END_TEST;
  }

  static bool log_wrap() {
    BEGIN_TEST;

    fbl::AllocChecker ac;
    ktl::unique_ptr<DLog> log = ktl::make_unique<DLog>(&ac);
    ASSERT_TRUE(ac.check(), "");

    char msg[] = "Hello World";
    const size_t kTruncateTarget = 5;  // An unaligned length somewhere in the middle of msg.
    const size_t pad = ALIGN4_TRUNC(DLOG_SIZE - sizeof(dlog_header) - kTruncateTarget);

    // We need to trigger some writes, but we don't care what we write, so we just need a buffer
    // large enough to be copied from. Doesn't much matter what's in it.
    char dummy[DLOG_MAX_DATA]{0};

    for (size_t to_write = pad; to_write;) {
      size_t write = to_write - sizeof(dlog_header);
      write = write > DLOG_MAX_DATA ? DLOG_MAX_DATA : write;

      log->write(DEBUGLOG_WARNING, 0, {dummy, write});
      to_write -= write + sizeof(dlog_header);
    }

    EXPECT_EQ(pad, log->head_);

    log->write(DEBUGLOG_WARNING, 0, {msg, sizeof(msg)});

    dlog_header* header = reinterpret_cast<dlog_header*>(log->data_ + pad);

    EXPECT_EQ(DEBUGLOG_WARNING, header->severity);
    EXPECT_EQ(0, header->flags);
    EXPECT_EQ(ZX_KOID_INVALID, header->pid);
    EXPECT_NE(ZX_KOID_INVALID, header->tid);
    ASSERT_EQ(sizeof(msg), header->datalen);

    uint8_t* msg_bytes = log->data_ + pad + sizeof(dlog_header);

    const size_t kTruncatedSize = DLOG_SIZE - pad - sizeof(dlog_header);

    EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(&msg[0]), msg_bytes, kTruncatedSize);
    EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(&msg[8]), log->data_, sizeof(msg) - kTruncatedSize);

    END_TEST;
  }

  // Read a record from the debuglog and verify its fields.
  static bool log_reader_read() {
    BEGIN_TEST;

    fbl::AllocChecker ac;
    ktl::unique_ptr<DLog> log = ktl::make_unique<DLog>(&ac);
    ASSERT_TRUE(ac.check());

    const zx_time_t now = current_time();

    char msg[] = "Message!";
    ASSERT_EQ(ZX_OK, log->write(DEBUGLOG_WARNING, 0, {msg, sizeof(msg)}));

    DlogReader reader;
    reader.InitializeForTest(log.get());
    size_t got;
    dlog_record_t rec{};
    ASSERT_EQ(ZX_OK, reader.Read(0, &rec, &got));
    ASSERT_EQ(sizeof(dlog_header) + sizeof(msg), got);
    EXPECT_EQ(0u, rec.hdr.preamble);
    // Sequence should start at 0.
    EXPECT_EQ(0u, rec.hdr.sequence);
    EXPECT_EQ(sizeof(msg), rec.hdr.datalen);
    EXPECT_EQ(DEBUGLOG_WARNING, rec.hdr.severity);
    EXPECT_EQ(0, rec.hdr.flags);
    EXPECT_GE(rec.hdr.timestamp, now);
    EXPECT_EQ(0ull, rec.hdr.pid);
    EXPECT_EQ(Thread::Current::Get()->tid(), rec.hdr.tid);

    reader.Disconnect();

    END_TEST;
  }

  // Write to the log, exceeding its capacity and see that data is lost.
  static bool log_reader_dataloss() {
    BEGIN_TEST;

    fbl::AllocChecker ac;
    ktl::unique_ptr<DLog> log = ktl::make_unique<DLog>(&ac);
    ASSERT_TRUE(ac.check(), "");

    DlogReader reader;
    reader.InitializeForTest(log.get());

    char msg[] = "Hello World";

    uint64_t num_written = 0;

    log->write(DEBUGLOG_WARNING, 0, {msg, sizeof(msg)});
    num_written++;

    dlog_record_t rec{};
    size_t got;

    ASSERT_EQ(ZX_OK, reader.Read(0, &rec, &got));
    ASSERT_EQ(sizeof(dlog_header) + sizeof(msg), got);

    EXPECT_EQ(DEBUGLOG_WARNING, rec.hdr.severity);
    EXPECT_EQ(0u, rec.hdr.preamble);
    EXPECT_EQ(0, rec.hdr.flags);
    EXPECT_EQ(ZX_KOID_INVALID, rec.hdr.pid);
    EXPECT_NE(ZX_KOID_INVALID, rec.hdr.tid);
    ASSERT_EQ(sizeof(msg), rec.hdr.datalen);

    EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(msg), reinterpret_cast<uint8_t*>(rec.data),
                    sizeof(msg));

    for (size_t i = 0; i < DLOG_SIZE; i += sizeof(dlog_header) + sizeof(msg)) {
      log->write(DEBUGLOG_WARNING, 0, {msg, sizeof(msg)});
      num_written++;
    }

    uint64_t num_read = 0;
    zx_status_t status;
    uint64_t expected_sequence = 0;
    uint64_t dropped = 0;
    while ((status = reader.Read(0, &rec, &got)) == ZX_OK) {
      num_read++;
      ASSERT_EQ(sizeof(dlog_header) + sizeof(msg), got);
      EXPECT_EQ(DEBUGLOG_WARNING, rec.hdr.severity);
      EXPECT_EQ(0u, rec.hdr.preamble);
      EXPECT_EQ(0, rec.hdr.flags);
      EXPECT_EQ(ZX_KOID_INVALID, rec.hdr.pid);
      EXPECT_NE(ZX_KOID_INVALID, rec.hdr.tid);
      ASSERT_EQ(sizeof(msg), rec.hdr.datalen);
      EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(msg), reinterpret_cast<uint8_t*>(rec.data),
                      sizeof(msg));
      dropped += (rec.hdr.sequence - expected_sequence);
      expected_sequence = rec.hdr.sequence + 1;
    }
    EXPECT_EQ(ZX_ERR_SHOULD_WAIT, status);

    // See that we read fewer records than we wrote.
    EXPECT_LT(num_read, num_written);

    EXPECT_EQ(0, rec.hdr.flags);
    EXPECT_EQ(ZX_KOID_INVALID, rec.hdr.pid);
    EXPECT_NE(ZX_KOID_INVALID, rec.hdr.tid);
    ASSERT_EQ(sizeof(msg), rec.hdr.datalen);

    EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(msg), reinterpret_cast<uint8_t*>(rec.data),
                    sizeof(msg));

    // See that all messages are acconted for.  That is, the number dropped matches the differnce.
    EXPECT_EQ(dropped, num_written - num_read);

    reader.Disconnect();
    END_TEST;
  }

  // Test that write fails with an error once the |DLog| has been shutdown.
  static bool shutdown() {
    BEGIN_TEST;

    fbl::AllocChecker ac;
    ktl::unique_ptr<DLog> log = ktl::make_unique<DLog>(&ac);
    ASSERT_TRUE(ac.check());

    // Write one message and see that it succeeds.
    char msg[] = "Message!";
    ASSERT_EQ(ZX_OK, log->write(DEBUGLOG_WARNING, 0, {msg, sizeof(msg)}));

    // Now ask the DLog to shutdown, write another, see that it fails.
    log->Shutdown(0);
    ASSERT_EQ(ZX_ERR_BAD_STATE, log->write(DEBUGLOG_WARNING, 0, msg));

    // See that there is only one message in the DLog.
    DlogReader reader;
    reader.InitializeForTest(log.get());
    size_t got;
    dlog_record_t rec{};
    ASSERT_EQ(ZX_OK, reader.Read(0, &rec, &got));
    ASSERT_EQ(sizeof(dlog_header) + sizeof(msg), got);
    ASSERT_EQ(ZX_ERR_SHOULD_WAIT, reader.Read(0, &rec, &got));
    reader.Disconnect();

    END_TEST;
  }
};

#define DEBUGLOG_UNITTEST(fname) UNITTEST(#fname, fname)

UNITTEST_START_TESTCASE(debuglog_tests)
DEBUGLOG_UNITTEST(DebuglogTests::log_format)
DEBUGLOG_UNITTEST(DebuglogTests::log_wrap)
DEBUGLOG_UNITTEST(DebuglogTests::log_reader_read)
DEBUGLOG_UNITTEST(DebuglogTests::log_reader_dataloss)
DEBUGLOG_UNITTEST(DebuglogTests::shutdown)
UNITTEST_END_TESTCASE(debuglog_tests, "debuglog_tests", "Debuglog test")
