// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/fit/defer.h>
#include <lib/ktrace/ktrace_internal.h>
#include <lib/unittest/unittest.h>

#include <ktl/algorithm.h>
#include <vm/vm_aspace.h>

namespace {

// A test version of KTraceState which overrides the ReportStaticNames and
// ReportThreadProcessNames behaviors for testing purposes.
struct TestKTraceState : public ::internal::KTraceState {
  TestKTraceState() {
    // disable diagnostic printfs in the test instances of KTrace we create.
    disable_diags_printfs_ = true;
  }

  void ReportStaticNames() override {
    last_static_name_report_time_ = current_time();
    ++static_name_report_count_;
  }

  void ReportThreadProcessNames() override {
    last_thread_name_report_time_ = current_time();
    ++thread_name_report_count_;
  }

  template <typename Checker>
  bool TestAllRecords(uint32_t& records_enumerated_out, const Checker& do_check) {
    BEGIN_TEST;

    const uint32_t limit = ktl::min(offset_.load(), bufsize_);
    uint32_t rd_offset = sizeof(ktrace_rec_32b_t) * 2;
    ASSERT_GE(offset_.load(), rd_offset);
    records_enumerated_out = 0;

    // We expect all trace buffers to start with a metadata records indicating the
    // version of the trace buffer format, and the clock resolution.  Verify that
    // these are present.
    ASSERT_NONNULL(buffer_);
    ASSERT_LE(rd_offset, limit);
    const ktrace_rec_32b_t& version_rec = reinterpret_cast<const ktrace_rec_32b_t*>(buffer_)[0];
    const ktrace_rec_32b_t& clock_res_rec = reinterpret_cast<const ktrace_rec_32b_t*>(buffer_)[1];

    EXPECT_EQ(TAG_VERSION, version_rec.tag);
    EXPECT_EQ(KTRACE_VERSION, version_rec.a);
    EXPECT_EQ(sizeof(ktrace_rec_32b_t), KTRACE_LEN(version_rec.tag));

    const uint64_t clock_res = ticks_per_second() / 1000;
    EXPECT_EQ(TAG_TICKS_PER_MS, clock_res_rec.tag);
    EXPECT_EQ(static_cast<uint32_t>(clock_res), clock_res_rec.a);
    EXPECT_EQ(static_cast<uint32_t>(clock_res >> 32), clock_res_rec.b);
    EXPECT_EQ(sizeof(ktrace_rec_32b_t), KTRACE_LEN(clock_res_rec.tag));

    // If something goes wrong while testing records, report _which_ record has
    // trouble, to assist with debugging.
    auto ReportRecord = fit::defer(
        [&]() { printf("\nFAILED while enumerating record (%u)\n", records_enumerated_out); });

    while (rd_offset < limit) {
      auto hdr = reinterpret_cast<const ktrace_header_t*>(buffer_ + rd_offset);
      // Zero length records are not legal.
      ASSERT_GT(KTRACE_LEN(hdr->tag), 0u);

      // Make sure the record matches expectations.
      ASSERT_TRUE(do_check(hdr));

      // Advance to the next record.
      ++records_enumerated_out;
      rd_offset += KTRACE_LEN(hdr->tag);
    }

    ReportRecord.cancel();
    END_TEST;
  }

  zx_time_t last_static_name_report_time_{0};
  zx_time_t last_thread_name_report_time_{0};
  uint32_t static_name_report_count_{0};
  uint32_t thread_name_report_count_{0};
};

}  // namespace

namespace ktrace_tests {

struct tests {
  static bool init_start() {
    BEGIN_TEST;

    constexpr uint32_t kAllGroups = KTRACE_GRP_ALL;

    {
      // Construct a ktrace state and initialize it, providing no group mask.
      // No buffer should be allocated, and no calls should be made to any of
      // the report hooks.  The only thing which should stick during this
      // operation is our target bufsize.
      TestKTraceState state;
      state.Init(4096, 0);

      EXPECT_NULL(state.buffer_);
      EXPECT_EQ(0u, state.bufsize_);
      EXPECT_EQ(4096u, state.target_bufsize_);
      EXPECT_EQ(0u, state.static_name_report_count_);
      EXPECT_EQ(0u, state.thread_name_report_count_);
      EXPECT_EQ(0u, state.grpmask_.load());

      // Attempting to start with no groups specified is not allowed.  We should
      // get "INVALID_ARGS" back.
      ASSERT_EQ(ZX_ERR_INVALID_ARGS, state.Start(0));

      // Now go ahead and call start.  This should cause the buffer to become
      // allocated, and for both static and thread names to be reported (static
      // before thread)
      ASSERT_OK(state.Start(kAllGroups));
      EXPECT_NONNULL(state.buffer_);
      EXPECT_GT(state.bufsize_, 0u);
      EXPECT_LE(state.bufsize_, state.target_bufsize_);
      EXPECT_EQ(4096u, state.target_bufsize_);
      EXPECT_EQ(1u, state.static_name_report_count_);
      EXPECT_EQ(1u, state.thread_name_report_count_);
      EXPECT_LE(state.last_static_name_report_time_, state.last_thread_name_report_time_);
      EXPECT_EQ(KTRACE_GRP_TO_MASK(kAllGroups), state.grpmask_.load());
    }

    {
      // Perform a similar test, but this time passing a non-zero group mask to
      // init.  This should cause tracing to go live immediately.
      TestKTraceState state;
      state.Init(4096, kAllGroups);

      EXPECT_NONNULL(state.buffer_);
      EXPECT_GT(state.bufsize_, 0u);
      EXPECT_LE(state.bufsize_, state.target_bufsize_);
      EXPECT_EQ(4096u, state.target_bufsize_);
      EXPECT_EQ(1u, state.static_name_report_count_);
      EXPECT_EQ(1u, state.thread_name_report_count_);
      EXPECT_LE(state.last_static_name_report_time_, state.last_thread_name_report_time_);
      EXPECT_EQ(KTRACE_GRP_TO_MASK(kAllGroups), state.grpmask_.load());
    }

    END_TEST;
  }

  static bool names() {
    BEGIN_TEST;
    // Create a small trace buffer and initialize it.
    constexpr uint32_t kGroups = 0x3;
    TestKTraceState state;
    state.Init(4096, kGroups);

    // Immediately after initialization, ktrace will write two metadata records
    // expressing the version of the trace buffer format, as well as the
    // resolution of the timestamps in the trace.  Make sure that the offset
    // reflects this.
    uint32_t expected_offset = sizeof(ktrace_rec_32b_t) * 2;
    ASSERT_EQ(expected_offset, state.offset_.load());

    constexpr struct NameTestVector {
      uint32_t tag;
      uint32_t id;
      uint32_t arg;
      bool always;
      bool expected_present;
      const char* name;
    } kTestVectors[] = {
        // clang-format off
      { KTRACE_TAG(0x1, 0x1,   8), 0xbaad0000, 0xf00d0000, false,  true, "Aria" },
      { KTRACE_TAG(0x2, 0x2,  16), 0xbaad0001, 0xf00d0001, false,  true, "Andrew Adrian" },
      { KTRACE_TAG(0x3, 0x4,  24), 0xbaad0002, 0xf00d0002, false, false, "Aurora Angel Aaron" },
      { KTRACE_TAG(0x4, 0x8,  32), 0xbaad0003, 0xf00d0003, false, false,
                   "Axel Addison Austin Aubrey" },
      { KTRACE_TAG(0x5, 0x1,  40), 0xbaad0004, 0xf00d0004,  true,  true,
                   "Aaliyah Anna Alice Amir Allison Ariana" },
      { KTRACE_TAG(0x6, 0x1,  48), 0xbaad0005, 0xf00d0005,  true,  true,
                   "Autumn Ayden Ashton August Adeline Adriel Athena" },
      { KTRACE_TAG(0x7, 0x1,  56), 0xbaad0006, 0xf00d0006,  true,  true,
                   "Archer Adalynn Arthur Alex Alaia Arianna", },
      { KTRACE_TAG(0x8, 0x1,  64), 0xbaad0007, 0xf00d0007,  true,  true,
                   "Ayla Alexandra Alan Ariel Adalyn Amaya Ace Amara Abraham" },
        // clang-format on
    };

    // A small helper which computes the expected size of a name test vector.
    auto ExpectedNameRecordSize = [](const NameTestVector& vec) -> uint32_t {
      // Strings are limited to ZX_MAX_NAME_LEN characters, including their null terminator.
      uint32_t string_storage =
          static_cast<uint32_t>(ktl::min(strlen(vec.name) + 1, ZX_MAX_NAME_LEN));

      // Total storage is the storage for the name header, plus the string
      // storage, all rounded up to the nearest 8 bytes.
      return (KTRACE_NAMESIZE + string_storage + 7) & ~0x7;
    };

    // Add all of the name test vectors to the trace buffer.  Verify that the
    // buffer grows as we would expect while we do so.
    uint32_t expected_present_count = 0;
    for (const auto& vec : kTestVectors) {
      ASSERT_EQ(expected_offset, state.offset_.load());
      state.WriteNameEtc(vec.tag, vec.id, vec.arg, vec.name, vec.always);
      if (vec.expected_present) {
        expected_offset += ExpectedNameRecordSize(vec);
        ++expected_present_count;
      }
      ASSERT_EQ(expected_offset, state.offset_.load());
    }

    // Now enumerate the buffer, and make sure that the records we expect are
    // present.
    uint32_t records_enumerated = 0;
    uint32_t vec_id = 0;
    auto checker = [&](const ktrace_header_t* hdr) -> bool {
      BEGIN_TEST;

      ASSERT_NONNULL(hdr);
      const ktrace_rec_name_t& rec = *(reinterpret_cast<const ktrace_rec_name_t*>(hdr));

      // Skip any records which should not have made it into the trace buffer.
      while ((vec_id < ktl::size(kTestVectors)) && !kTestVectors[vec_id].expected_present) {
        ++vec_id;
      }

      // We should still have a test vector to compare.
      ASSERT_LT(vec_id, ktl::size(kTestVectors));
      const auto& vec = kTestVectors[vec_id];

      // The individual fields of the tag should all match, except for the
      // length, which should have been overwritten when the record was
      // added.
      EXPECT_EQ(ExpectedNameRecordSize(vec), KTRACE_LEN(rec.tag));
      EXPECT_EQ(KTRACE_GROUP(vec.tag), KTRACE_GROUP(rec.tag));
      EXPECT_EQ(KTRACE_EVENT(vec.tag), KTRACE_EVENT(rec.tag));
      EXPECT_EQ(KTRACE_FLAGS(vec.tag), KTRACE_FLAGS(rec.tag));

      // ID and arg should have been directly copied into the record.
      EXPECT_EQ(vec.id, rec.id);
      EXPECT_EQ(vec.arg, rec.arg);

      // Name should match, up to the limit of ZX_MAX_NAME_LEN - 1, and
      // the record should be null terminated.
      const uint32_t expected_name_len =
          static_cast<uint32_t>(ktl::min(strlen(vec.name), ZX_MAX_NAME_LEN - 1));
      ASSERT_EQ(expected_name_len, strlen(rec.name));
      EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>(vec.name),
                      reinterpret_cast<const uint8_t*>(rec.name), expected_name_len);
      EXPECT_EQ(0, rec.name[expected_name_len]);

      ++vec_id;
      END_TEST;
    };

    ASSERT_TRUE(state.TestAllRecords(records_enumerated, checker));
    EXPECT_EQ(expected_present_count, records_enumerated);

    END_TEST;
  }

  static bool write_records() {
    BEGIN_TEST;

    // Create a small trace buffer and initialize it.
    constexpr uint32_t kGroups = 0x3;
    TestKTraceState state;
    state.Init(4096, kGroups);

    uint32_t expected_offset = sizeof(ktrace_rec_32b_t) * 2;
    ASSERT_EQ(expected_offset, state.offset_.load());

    // Exercise each of the supported forms of WriteRecord.  There are 7 in total:
    // 1) No payload
    // 2) Payloads with 1-4 u32 arguments.
    // 3) Payloads with 1-2 u64 arguments.
    constexpr uint64_t kFirstTS = 0x1234567890abcdef;
    constexpr uint32_t kBaseSz = sizeof(ktrace_header_t);
    constexpr uint32_t kBaseU32 = 0xbaad0000;
    constexpr uint64_t kBaseU64 = 0xbaadbaadf00d0000;

    auto MakeTag = [](uint32_t wr_ndx, uint32_t arg_cnt, uint32_t arg_sz) {
      const uint32_t sz = (kBaseSz + (arg_cnt * arg_sz) + 0x7) & ~0x7;
      return KTRACE_TAG(wr_ndx + 1, 1, sz);
    };
    auto TS = [](uint32_t wr_ndx) { return kFirstTS + wr_ndx; };
    auto U32 = [](uint32_t wr_ndx, uint32_t arg) { return kBaseU32 + (wr_ndx * 16) + arg; };
    auto U64 = [](uint32_t wr_ndx, uint32_t arg) { return kBaseU64 + (wr_ndx * 16) + arg; };

    state.WriteRecord(MakeTag(0, 0, 0), TS(0));
    state.WriteRecord(MakeTag(1, 1, 4), TS(1), U32(1, 0));
    state.WriteRecord(MakeTag(2, 2, 4), TS(2), U32(2, 0), U32(2, 1));
    state.WriteRecord(MakeTag(3, 3, 4), TS(3), U32(3, 0), U32(3, 1), U32(3, 2));
    state.WriteRecord(MakeTag(4, 4, 4), TS(4), U32(4, 0), U32(4, 1), U32(4, 2), U32(4, 3));
    state.WriteRecord(MakeTag(5, 1, 8), TS(5), U64(5, 0));
    state.WriteRecord(MakeTag(6, 2, 8), TS(6), U64(6, 0), U64(6, 1));

    // Now read the records back out and verify their contents.
    constexpr struct {
      uint32_t num_args;
      uint32_t arg_size;
    } kTestVectors[] = {{0, 0}, {1, 4}, {2, 4}, {3, 4}, {4, 4}, {1, 8}, {2, 8}};

    uint32_t records_enumerated = 0;
    uint32_t vec_id = 0;
    auto checker = [&](const ktrace_header_t* hdr) -> bool {
      BEGIN_TEST;

      ASSERT_NONNULL(hdr);
      const ktrace_header_t& rec = *(reinterpret_cast<const ktrace_header_t*>(hdr));

      ASSERT_LT(vec_id, ktl::size(kTestVectors));
      const auto& vec = kTestVectors[vec_id];

      const uint32_t expected_size = static_cast<uint32_t>(
          (sizeof(ktrace_header_t) + (vec.num_args * vec.arg_size) + 0x7) & ~0x7);
      const uint32_t expected_tag = MakeTag(vec_id, vec.num_args, vec.arg_size);

      // Check the tag fields
      EXPECT_EQ(expected_size, KTRACE_LEN(rec.tag));
      EXPECT_EQ(KTRACE_GROUP(expected_tag), KTRACE_GROUP(rec.tag));
      EXPECT_EQ(KTRACE_EVENT(expected_tag), KTRACE_EVENT(rec.tag));
      EXPECT_EQ(KTRACE_FLAGS(expected_tag), KTRACE_FLAGS(rec.tag));

      // Check the timestamp
      EXPECT_EQ(TS(vec_id), rec.ts);

      // Check the payload
      switch (vec.arg_size) {
        case 0:
          break;

        case 4: {
          auto payload = reinterpret_cast<const uint32_t*>(hdr + 1);
          for (uint32_t i = 0; i < vec.num_args; ++i) {
            ASSERT_EQ(U32(vec_id, i), payload[i]);
          }
        } break;

        case 8: {
          auto payload = reinterpret_cast<const uint64_t*>(hdr + 1);
          for (uint32_t i = 0; i < vec.num_args; ++i) {
            ASSERT_EQ(U64(vec_id, i), payload[i]);
          }
        } break;

        default:
          ASSERT_TRUE(false);
          break;
      }

      ++vec_id;
      END_TEST;
    };

    ASSERT_TRUE(state.TestAllRecords(records_enumerated, checker));
    EXPECT_EQ(7u, records_enumerated);

    END_TEST;
  }

  static bool write_tiny_records() {
    BEGIN_TEST;

    // Create a small trace buffer and initialize it.
    constexpr uint32_t kGroups = 0x3;
    TestKTraceState state;
    state.Init(4096, kGroups);

    uint32_t expected_offset = sizeof(ktrace_rec_32b_t) * 2;
    ASSERT_EQ(expected_offset, state.offset_.load());

    // Write a some tiny records
    constexpr uint32_t kBaseArg = 0xbaad0000;
    constexpr uint32_t kCnt = 15;

    auto MakeTag = [](uint32_t wr_ndx) { return KTRACE_TAG(wr_ndx + 1, 1, 0); };
    auto Arg = [](uint32_t wr_ndx) { return kBaseArg + wr_ndx; };
    uint64_t prev_ts = 0;

    for (uint32_t i = 0; i < kCnt; ++i) {
      state.WriteRecordTiny(MakeTag(i), Arg(i));
    }

    uint32_t records_enumerated = 0;
    uint32_t vec_id = 0;
    auto checker = [&](const ktrace_header_t* hdr) -> bool {
      BEGIN_TEST;

      ASSERT_NONNULL(hdr);
      const ktrace_header_t& rec = *(reinterpret_cast<const ktrace_header_t*>(hdr));

      // Check the tag fields
      const uint32_t expected_tag = MakeTag(vec_id);
      EXPECT_EQ(16u, KTRACE_LEN(rec.tag));
      EXPECT_EQ(KTRACE_GROUP(expected_tag), KTRACE_GROUP(rec.tag));
      EXPECT_EQ(KTRACE_EVENT(expected_tag), KTRACE_EVENT(rec.tag));
      EXPECT_EQ(KTRACE_FLAGS(expected_tag), KTRACE_FLAGS(rec.tag));

      // Check the timestamp.  We are not allowed to provide an explicit
      // timestamp, so the best we can do is make sure that they are
      // monotonically increasing.
      EXPECT_LE(prev_ts, rec.ts);
      prev_ts = rec.ts;

      // Check the payload.  Tiny records store their argument in the TID
      // field.
      EXPECT_LE(Arg(vec_id), rec.tid);

      ++vec_id;
      END_TEST;
    };

    ASSERT_TRUE(state.TestAllRecords(records_enumerated, checker));
    EXPECT_EQ(kCnt, records_enumerated);

    END_TEST;
  }

  static bool saturation() {
    BEGIN_TEST;

    // Create a small trace buffer and initialize it.
    constexpr uint32_t kGroups = 0x3;
    TestKTraceState state;
    state.Init(4096 + 8, kGroups);

    // Figure out how many 32 byte records we should be able to fit into our
    // buffer.  Account for the fact that the current implementation reserves
    // enough space for the largest possible record to fit just after then end
    // of the nominal buffer size, and allows the final record to go over the
    // end of the buffer.
    ASSERT_GE(state.bufsize_, sizeof(ktrace_rec_32b_t) * 2);
    const uint32_t max_records =
        static_cast<uint32_t>(((state.bufsize_ - sizeof(ktrace_rec_32b_t) * 2) + 31) / 32);

    // Write the maximum number of records to the buffer.
    for (uint32_t i = 0; i < max_records; ++i) {
      state.WriteRecord(TAG_PROBE_32(1), kRecordCurrentTimestamp, 5u, 6u);
    }

    // The buffer will not think that it is full just yet.
    uint32_t rcnt = 0;
    auto checker = [&](const ktrace_header_t* hdr) -> bool { return true; };
    EXPECT_EQ(KTRACE_GRP_TO_MASK(kGroups), state.grpmask_.load());
    EXPECT_FALSE(state.buffer_full_.load());
    EXPECT_TRUE(state.TestAllRecords(rcnt, checker));
    EXPECT_EQ(max_records, rcnt);

    // Now write one more record, this time with a different payload.
    state.WriteRecord(TAG_PROBE_32(1), kRecordCurrentTimestamp, 0xbaadf00d, 0xf00dbaad);

    // The buffer should not think that it is full, and all of the original records should be
    // present (but not the new one).
    auto payload_checker = [&](const ktrace_header_t* hdr) -> bool {
      BEGIN_TEST;
      ASSERT_NONNULL(hdr);

      const ktrace_rec_32b_t& rec = *(reinterpret_cast<const ktrace_rec_32b_t*>(hdr));
      EXPECT_EQ(32u, KTRACE_LEN(rec.tag));
      EXPECT_EQ(5u, rec.a);
      EXPECT_EQ(6u, rec.b);

      END_TEST;
    };
    EXPECT_EQ(0u, state.grpmask_.load());
    EXPECT_TRUE(state.buffer_full_.load());
    EXPECT_TRUE(state.TestAllRecords(rcnt, payload_checker));
    EXPECT_EQ(max_records, rcnt);

    END_TEST;
  }

  static bool rewind() {
    BEGIN_TEST;

    // Create a small trace buffer and initialize it.
    constexpr uint32_t kGroups = 0x3;
    TestKTraceState state;
    state.Init(4096, kGroups);

    uint32_t expected_offset = sizeof(ktrace_rec_32b_t) * 2;
    EXPECT_EQ(expected_offset, state.offset_.load());

    // Count the number of records.  There should not be any right now.
    uint32_t rcnt = 0;
    auto checker = [&](const ktrace_header_t* hdr) -> bool { return true; };
    EXPECT_TRUE(state.TestAllRecords(rcnt, checker));
    EXPECT_EQ(0u, rcnt);

    // Write a couple of records.
    state.WriteRecord(TAG_PROBE_32(1), kRecordCurrentTimestamp, 5u, 6u);
    state.WriteRecord(TAG_PROBE_32(1), kRecordCurrentTimestamp, 5u, 6u);

    // The offset should have moved, and the number of records in the buffer should now be 2.
    EXPECT_LT(expected_offset, state.offset_.load());
    EXPECT_EQ(KTRACE_GRP_TO_MASK(kGroups), state.grpmask_.load());
    EXPECT_TRUE(state.TestAllRecords(rcnt, checker));
    EXPECT_EQ(2u, rcnt);

    // Stop and Rewind.  The offset should return to the beginning, and there
    // should be no records in the buffer.
    ASSERT_OK(state.Stop());
    ASSERT_OK(state.Rewind());
    EXPECT_EQ(expected_offset, state.offset_.load());
    EXPECT_EQ(0u, state.grpmask_.load());
    EXPECT_TRUE(state.TestAllRecords(rcnt, checker));
    EXPECT_EQ(0u, rcnt);

    // Start again, and this time saturate the buffer
    ASSERT_OK(state.Start(kGroups));
    while (!state.buffer_full_.load()) {
      state.WriteRecord(TAG_PROBE_32(1), kRecordCurrentTimestamp, 5u, 6u);
    }
    EXPECT_EQ(0u, state.grpmask_.load());
    EXPECT_TRUE(state.TestAllRecords(rcnt, checker));
    EXPECT_LT(2u, rcnt);

    // Finally, stop and rewind again.  The offset should return to the
    // beginning, and there should be no records in the buffer.
    ASSERT_OK(state.Stop());
    ASSERT_OK(state.Rewind());
    EXPECT_EQ(expected_offset, state.offset_.load());
    EXPECT_EQ(0u, state.grpmask_.load());
    EXPECT_TRUE(state.TestAllRecords(rcnt, checker));
    EXPECT_EQ(0u, rcnt);

    END_TEST;
  }

  static bool state_checks() {
    BEGIN_TEST;

    constexpr uint32_t kAllGroups = KTRACE_GRP_ALL;
    constexpr uint32_t kSomeGroups = 0x3;

    {
      TestKTraceState state;
      state.Init(4096, 0);

      // We didn't provide a non-zero initial set of groups, so the trace should
      // not be started right now.  Stopping, rewinding, and reading are all
      // legal (although, stopping does nothing).  We have not allocated our
      // buffer yet, so not even the static metadata should be available to
      // read.
      ASSERT_OK(state.Stop());
      ASSERT_EQ(0u, state.ReadUser(nullptr, 0, 0));
      ASSERT_OK(state.Rewind());
      ASSERT_EQ(KTRACE_GRP_TO_MASK(0u), state.grpmask_.load());

      // Starting should succeed.
      ASSERT_OK(state.Start(kAllGroups));
      ASSERT_EQ(KTRACE_GRP_TO_MASK(kAllGroups), state.grpmask_.load());

      // Now that we are started, rewinding or should fail because of the state
      // check.
      ASSERT_EQ(ZX_ERR_BAD_STATE, state.Rewind());
      ASSERT_EQ(ZX_ERR_BAD_STATE, state.ReadUser(nullptr, 0, 0));
      ASSERT_EQ(KTRACE_GRP_TO_MASK(kAllGroups), state.grpmask_.load());

      // Starting while already started should succeed, but change the active
      // group mask.
      ASSERT_OK(state.Start(kSomeGroups));
      ASSERT_EQ(KTRACE_GRP_TO_MASK(kSomeGroups), state.grpmask_.load());

      // Stopping is still OK, and actually does something now (it clears the
      // group mask).
      ASSERT_OK(state.Stop());
      ASSERT_EQ(KTRACE_GRP_TO_MASK(0u), state.grpmask_.load());

      // Specifically, now that we are stopped, we can read, rewind, and stop
      // again.  Since we have started before, we expect that the amount of data
      // available to read should be equal to the size of the two static
      // metadata records.
      const ssize_t expected_size = sizeof(ktrace_rec_32b_t) * 2;
      ASSERT_EQ(expected_size, state.ReadUser(nullptr, 0, 0));
      ASSERT_OK(state.Rewind());
      ASSERT_OK(state.Stop());
    }

    {
      // Same checks as before, but this time start in the started state after
      // init by providing a non-zero set of groups.
      TestKTraceState state;
      state.Init(4096, kAllGroups);
      ASSERT_EQ(KTRACE_GRP_TO_MASK(kAllGroups), state.grpmask_.load());

      // We are started, so rewinding or reading should fail because of the
      // state check.
      ASSERT_EQ(ZX_ERR_BAD_STATE, state.Rewind());
      ASSERT_EQ(ZX_ERR_BAD_STATE, state.ReadUser(nullptr, 0, 0));
      ASSERT_EQ(KTRACE_GRP_TO_MASK(kAllGroups), state.grpmask_.load());

      // "Restarting" should change the active group mask.
      ASSERT_OK(state.Start(kSomeGroups));
      ASSERT_EQ(KTRACE_GRP_TO_MASK(kSomeGroups), state.grpmask_.load());

      // Stopping should work.
      ASSERT_OK(state.Stop());
      ASSERT_EQ(KTRACE_GRP_TO_MASK(0u), state.grpmask_.load());

      // Stopping again, rewinding, and reading are all OK now.
      const ssize_t expected_size = sizeof(ktrace_rec_32b_t) * 2;
      ASSERT_EQ(expected_size, state.ReadUser(nullptr, 0, 0));
      ASSERT_OK(state.Rewind());
      ASSERT_OK(state.Stop());
    }

    END_TEST;
  }
};

}  // namespace ktrace_tests

UNITTEST_START_TESTCASE(ktrace_tests)
UNITTEST("init/start", ktrace_tests::tests::init_start)
UNITTEST("names", ktrace_tests::tests::names)
UNITTEST("write record", ktrace_tests::tests::write_records)
UNITTEST("write tiny record", ktrace_tests::tests::write_tiny_records)
UNITTEST("saturation", ktrace_tests::tests::saturation)
UNITTEST("rewind", ktrace_tests::tests::rewind)
UNITTEST("state check", ktrace_tests::tests::state_checks)
UNITTEST_END_TESTCASE(ktrace_tests, "ktrace", "KTrace tests")
