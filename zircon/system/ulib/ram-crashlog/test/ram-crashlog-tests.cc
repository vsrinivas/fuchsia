// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cksum.h>
#include <stdint.h>
#include <zircon/boot/crash-reason.h>
#include <zircon/errors.h>

#include <array>
#include <vector>

#include <ram-crashlog/ram-crashlog.h>
#include <zxtest/zxtest.h>

namespace {

// The buffer we will use as our target for serializing the crashlog during testing.
uint8_t crashlog_buffer[256];
constexpr uint32_t TEST_PAYLOAD_MAX = sizeof(crashlog_buffer) - sizeof(ram_crashlog_t);
constexpr uint8_t TEST_PAYLOAD_FILL = 0xA5;
static_assert(sizeof(crashlog_buffer) > sizeof(ram_crashlog_t),
              "Test buffer must be able to hold _some_ payload");
static_assert(TEST_PAYLOAD_MAX >= 2,
              "Test buffer must be able to hold a payload of at least 2 bytes");

constexpr const char LONG_PAYLOAD[] =
    "Four score and seven years ago our fathers brought forth on this continent, a new nation, "
    "conceived in Liberty, and dedicated to the proposition that all men are created equal.  Now "
    "we are engaged in a great civil war, testing whether that nation, or any nation so conceived "
    "and so dedicated, can long endure. We are met on a great battle-field of that war. We have "
    "come to dedicate a portion of that field, as a final resting place for those who here gave "
    "their lives that that nation might live. It is altogether fitting and proper that we should "
    "do this.  But, in a larger sense, we can not dedicate—we can not consecrate—we can not "
    "hallow—this ground. The brave men, living and dead, who struggled here, have consecrated it, "
    "far above our poor power to add or detract. The world will little note, nor long remember "
    "what we say here, but it can never forget what they did here. It is for us the living, "
    "rather, to be dedicated here to the unfinished work which they who fought here have thus far "
    "so nobly advanced. It is rather for us to be here dedicated to the great task remaining "
    "before us—that from these honored dead we take increased devotion to that cause for which "
    "they gave the last full measure of devotion—that we here highly resolve that these dead shall "
    "not have died in vain—that this nation, under God, shall have a new birth of freedom—and that "
    "government of the people, by the people, for the people, shall not perish from the earth.";
constexpr uint32_t LONG_PAYLOAD_LEN = sizeof(LONG_PAYLOAD) - 1;
static_assert(LONG_PAYLOAD_LEN > TEST_PAYLOAD_MAX,
              "Long payload must be longer than the max test payload");

// A constant test vector we use in several tests.  This "crashlog" image
// contains two valid headers, and indicates hdr[0] as being the active header.
// Starting from this template, many types of tests can be constructed.
constexpr ram_crashlog_t TEST_LOG = {
    .magic = RAM_CRASHLOG_MAGIC_0,
    .hdr =
        {
            {
                .uptime = 0xabcde,
                .reason = ZirconCrashReason::Panic,
                .payload_len = TEST_PAYLOAD_MAX,
                .payload_crc32 = 0xaa0b6321,  // CRC for a payload of all 0xA5s
                .header_crc32 = 0xd8d492b1,
            },
            {
                .uptime = 0x12345,
                .reason = ZirconCrashReason::Unknown,
                .payload_len = 0,
                .payload_crc32 = 0x0,
                .header_crc32 = 0xe082e1b7,
            },
        },
};

TEST(RamCrashlogTestCase, ValidBufferRequired) {
  zx_status_t res = ram_crashlog_stow(nullptr, 0, nullptr, 0, ZirconCrashReason::Unknown, 0);
  EXPECT_STATUS(res, ZX_ERR_INVALID_ARGS);

  res = ram_crashlog_stow(crashlog_buffer, sizeof(crashlog_buffer), nullptr, 25,
                          ZirconCrashReason::Unknown, 0);
  EXPECT_STATUS(res, ZX_ERR_INVALID_ARGS);
}

TEST(RamCrashlogTestCase, BufferTooSmall) {
  // Attempt to stash a crashlog into a location which cannot possibly fit the
  // internal header and verify that it informs us that our buffer is too small.
  uint8_t tiny_buf[1];
  zx_status_t res =
      ram_crashlog_stow(tiny_buf, sizeof(tiny_buf), nullptr, 0, ZirconCrashReason::Unknown, 0);
  EXPECT_STATUS(res, ZX_ERR_BUFFER_TOO_SMALL);

  // Likewise, we cannot recover a crashlog from a user supplied buffer which is too small to
  // possibly hold a crashlog.
  recovered_ram_crashlog_t rlog;
  res = ram_crashlog_recover(tiny_buf, sizeof(tiny_buf), &rlog);
  EXPECT_STATUS(res, ZX_ERR_BUFFER_TOO_SMALL);
}

TEST(RamCrashlogTestCase, ValidReasonRequired) {
  // When stowing a crashlog, require that the crash reason given is a valid one
  // (even if the reason is unknown).
  //
  struct Reason {
    ZirconCrashReason reason;
    bool is_valid;
  };

  constexpr std::array REASONS{
      Reason{ZirconCrashReason::Unknown, true},
      Reason{ZirconCrashReason::NoCrash, true},
      Reason{ZirconCrashReason::Oom, true},
      Reason{ZirconCrashReason::Panic, true},
      Reason{ZirconCrashReason::SoftwareWatchdog, true},
      Reason{ZirconCrashReason::Invalid, false},
      Reason{static_cast<ZirconCrashReason>(0xbaadf00d), false},
  };

  for (const auto& r : REASONS) {
    zx_status_t res =
        ram_crashlog_stow(crashlog_buffer, sizeof(crashlog_buffer), nullptr, 0, r.reason, 0);
    EXPECT_STATUS(res, r.is_valid ? ZX_OK : ZX_ERR_INVALID_ARGS);
  }
}

TEST(RamCrashlogTestCase, IntegrityChecks) {
  // Start by using our test header template to simulate a crashlog stowed in
  // RAM, and make sure that it passes the default integrity checks.
  ram_crashlog_t& log = *(reinterpret_cast<ram_crashlog_t*>(crashlog_buffer));
  uint8_t* payload = reinterpret_cast<uint8_t*>(&log + 1);
  memset(crashlog_buffer, TEST_PAYLOAD_FILL, sizeof(crashlog_buffer));
  log = TEST_LOG;

  // Recover the log, and make sure that the recovered data matches what we
  // expect.  Right now, the magic number indicates that the active header is
  // hdr[0].
  recovered_ram_crashlog_t rlog;
  zx_status_t res;

  res = ram_crashlog_recover(crashlog_buffer, sizeof(crashlog_buffer), &rlog);
  ASSERT_OK(res);
  EXPECT_EQ(TEST_LOG.hdr[0].uptime, rlog.uptime);
  EXPECT_EQ(TEST_LOG.hdr[0].reason, rlog.reason);
  EXPECT_EQ(TEST_LOG.hdr[0].payload_len, rlog.payload_len);
  EXPECT_TRUE(rlog.payload_valid);
  EXPECT_EQ(payload, rlog.payload);

  // Corrupt the payload and verify that the log is still recoverable, but that
  // it clearly indicates that the payload portion of the log may have been
  // damaged.
  payload[0] = ~payload[0];
  res = ram_crashlog_recover(crashlog_buffer, sizeof(crashlog_buffer), &rlog);
  ASSERT_OK(res);
  EXPECT_EQ(TEST_LOG.hdr[0].uptime, rlog.uptime);
  EXPECT_EQ(TEST_LOG.hdr[0].reason, rlog.reason);
  EXPECT_EQ(TEST_LOG.hdr[0].payload_len, rlog.payload_len);
  EXPECT_FALSE(rlog.payload_valid);
  EXPECT_EQ(payload, rlog.payload);

  // Fix the damage we just did to the payload, then update the payload length
  // length in hdr[0] to be impossibly long (longer than the buffer we provide,
  // once the fixed overhead is accounted for).  Make sure to update the header
  // CRC.  Finally, attempt to recover the log.  Again, this should succeed, but
  // indicate that the payload may be damaged.  Additionally, it should report a
  // length which is equal to the space we have remaining in the crashlog buffer
  // after the log headers.
  payload[0] = TEST_PAYLOAD_FILL;
  log.hdr[0].payload_len = sizeof(crashlog_buffer);
  log.hdr[0].header_crc32 = crc32(0, reinterpret_cast<const uint8_t*>(&log.hdr[0]),
                                  offsetof(ram_crashlog_header_t, header_crc32));
  res = ram_crashlog_recover(crashlog_buffer, sizeof(crashlog_buffer), &rlog);
  ASSERT_OK(res);
  EXPECT_EQ(TEST_LOG.hdr[0].uptime, rlog.uptime);
  EXPECT_EQ(TEST_LOG.hdr[0].reason, rlog.reason);
  EXPECT_EQ(TEST_PAYLOAD_MAX, rlog.payload_len);
  EXPECT_FALSE(rlog.payload_valid);
  EXPECT_EQ(payload, rlog.payload);

  // Corrupt the header by restoring the old payload length, but not updating
  // the header CRC, then attempt to recover the log.  This should simply fail.
  // We cannot currently recover the log if the active header is damaged.
  log.hdr[0].payload_len = TEST_LOG.hdr[0].payload_len;
  res = ram_crashlog_recover(crashlog_buffer, sizeof(crashlog_buffer), &rlog);
  EXPECT_STATUS(ZX_ERR_IO_DATA_INTEGRITY, res);

  // Flip the magic number to indicate that the other header is active, and
  // re-verify.  This should succeed, even though we have corrupted hdr[0].
  // This header should indicate a valid but zero-length payload.
  log.magic = RAM_CRASHLOG_MAGIC_1;
  res = ram_crashlog_recover(crashlog_buffer, sizeof(crashlog_buffer), &rlog);
  ASSERT_OK(res);
  EXPECT_EQ(TEST_LOG.hdr[1].uptime, rlog.uptime);
  EXPECT_EQ(TEST_LOG.hdr[1].reason, rlog.reason);
  EXPECT_EQ(TEST_LOG.hdr[1].payload_len, rlog.payload_len);
  EXPECT_TRUE(rlog.payload_valid);
  EXPECT_NULL(rlog.payload);

  // Corrupt the contents of hdr[1] and make sure that it fails to recover.
  log.hdr[1].payload_len = 1;
  res = ram_crashlog_recover(crashlog_buffer, sizeof(crashlog_buffer), &rlog);
  EXPECT_STATUS(ZX_ERR_IO_DATA_INTEGRITY, res);

  // Finally, fix all of the headers by going back to the template image, but
  // corrupt the magic number so that we don't know which header (if any) is
  // active.  This should also produce a DATA_INTEGRITY error.
  log = TEST_LOG;
  log.magic = 0x0123456789ABCDEF;
  res = ram_crashlog_recover(crashlog_buffer, sizeof(crashlog_buffer), &rlog);
  EXPECT_STATUS(ZX_ERR_IO_DATA_INTEGRITY, res);
}

TEST(RamCrashlogTestCase, Stow) {
  // Start with an invalid crashlog state (we just use a buffer full of 0s).
  // Verify that this fails to recover.
  ram_crashlog_t& log = *(reinterpret_cast<ram_crashlog_t*>(crashlog_buffer));
  uint8_t* payload = reinterpret_cast<uint8_t*>(&log + 1);
  memset(crashlog_buffer, 0, sizeof(crashlog_buffer));

  recovered_ram_crashlog_t rlog;
  zx_status_t res;
  res = ram_crashlog_recover(crashlog_buffer, sizeof(crashlog_buffer), &rlog);
  EXPECT_STATUS(ZX_ERR_IO_DATA_INTEGRITY, res);

  // Now stow a new log with no payload and verify that it holds the values we
  // told it to.
  res = ram_crashlog_stow(crashlog_buffer, sizeof(crashlog_buffer), nullptr, 0,
                          ZirconCrashReason::Unknown, 4599);
  ASSERT_OK(res);

  res = ram_crashlog_recover(crashlog_buffer, sizeof(crashlog_buffer), &rlog);
  ASSERT_OK(res);
  EXPECT_EQ(4599, rlog.uptime);
  EXPECT_EQ(ZirconCrashReason::Unknown, rlog.reason);
  EXPECT_EQ(0, rlog.payload_len);
  EXPECT_TRUE(rlog.payload_valid);
  EXPECT_NULL(rlog.payload);

  // While we do not specific which header the implementation will use when
  // replacing an invalid log with a valid log, we _do_ specify that the log
  // headers should be double buffered.  Now that the implementation has chosen
  // a header, we expect the choice of header to toggle each time we stow a new
  // log.
  uint64_t expected_magic =
      (log.magic == RAM_CRASHLOG_MAGIC_0) ? RAM_CRASHLOG_MAGIC_1 : RAM_CRASHLOG_MAGIC_0;

  // Stow a new crashlog, but this time stash a payload which fits in our space, but
  // does not fill it entirely.
  constexpr uint32_t TO_STOW = TEST_PAYLOAD_MAX / 2;
  memset(payload, 0, TEST_PAYLOAD_MAX);
  res = ram_crashlog_stow(crashlog_buffer, sizeof(crashlog_buffer), LONG_PAYLOAD, TO_STOW,
                          ZirconCrashReason::Oom, 9945);
  ASSERT_OK(res);

  res = ram_crashlog_recover(crashlog_buffer, sizeof(crashlog_buffer), &rlog);
  ASSERT_OK(res);
  EXPECT_EQ(9945, rlog.uptime);
  EXPECT_EQ(ZirconCrashReason::Oom, rlog.reason);
  EXPECT_EQ(TO_STOW, rlog.payload_len);
  EXPECT_TRUE(rlog.payload_valid);
  EXPECT_BYTES_EQ(LONG_PAYLOAD, rlog.payload, rlog.payload_len);
  EXPECT_EQ(expected_magic,
            log.magic);  // Peek under the hood and validate this implementation detail.

  // Finally, attempt to stash a log with a payload which does _not_ fit into
  // our available space.  This should succeed, but the payload (once recovered)
  // should be truncated.
  expected_magic =
      (log.magic == RAM_CRASHLOG_MAGIC_0) ? RAM_CRASHLOG_MAGIC_1 : RAM_CRASHLOG_MAGIC_0;

  memset(payload, 0xFF, TEST_PAYLOAD_MAX);
  res = ram_crashlog_stow(crashlog_buffer, sizeof(crashlog_buffer), LONG_PAYLOAD, LONG_PAYLOAD_LEN,
                          ZirconCrashReason::Panic, 314159);
  ASSERT_OK(res);

  res = ram_crashlog_recover(crashlog_buffer, sizeof(crashlog_buffer), &rlog);
  ASSERT_OK(res);
  EXPECT_EQ(314159, rlog.uptime);
  EXPECT_EQ(ZirconCrashReason::Panic, rlog.reason);
  EXPECT_EQ(TEST_PAYLOAD_MAX, rlog.payload_len);
  EXPECT_TRUE(rlog.payload_valid);
  EXPECT_BYTES_EQ(LONG_PAYLOAD, rlog.payload, rlog.payload_len);
  EXPECT_EQ(expected_magic,
            log.magic);  // Peek under the hood and validate this implementation detail.
}

}  // namespace
