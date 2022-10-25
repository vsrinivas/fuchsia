// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/fit/defer.h>
#include <lib/ktrace/ktrace_internal.h>
#include <lib/unittest/unittest.h>

#include <ktl/algorithm.h>
#include <ktl/limits.h>
#include <ktl/unique_ptr.h>
#include <vm/vm_aspace.h>

#include <ktl/enforce.h>

namespace ktrace_tests {

// A test version of KTraceState which overrides the ReportStaticNames and
// ReportThreadProcessNames behaviors for testing purposes.
class TestKTraceState : public ::internal::KTraceState {
 public:
  using StartMode = internal::KTraceState::StartMode;
  static constexpr uint32_t kDefaultBufferSize = 4096;

  // Figure out how many 32 byte records we should be able to fit into our
  // default buffer size, minus the two metadata records we consume up front.  Since we
  // always allocate buffers in multiples of page size, we should be able to
  // assert that this is an integral number of records.
  static_assert(kDefaultBufferSize > (sizeof(ktrace_rec_32b_t) * 2));
  static_assert(((kDefaultBufferSize - (sizeof(ktrace_rec_32b_t) * 2)) % 32) == 0);
  static constexpr uint32_t kMax32bRecords =
      (kDefaultBufferSize - (sizeof(ktrace_rec_32b_t) * 2)) / 32;

  static bool InitStartTest() {
    BEGIN_TEST;

    constexpr uint32_t kAllGroups = KTRACE_GRP_ALL;

    {
      // Construct a ktrace state and initialize it, providing no group mask.
      // No buffer should be allocated, and no calls should be made to any of
      // the report hooks.  The only thing which should stick during this
      // operation is our target bufsize.
      TestKTraceState state;
      ASSERT_TRUE(state.Init(kDefaultBufferSize, 0));
      {
        Guard<SpinLock, IrqSave> guard{&state.write_lock_};
        EXPECT_NULL(state.buffer_);
        EXPECT_EQ(0u, state.bufsize_);
        EXPECT_EQ(kDefaultBufferSize, state.target_bufsize_);
        EXPECT_EQ(0u, state.static_name_report_count_);
        EXPECT_EQ(0u, state.thread_name_report_count_);
        EXPECT_EQ(0u, state.grpmask());
      }

      // Attempting to start with no groups specified is not allowed.  We should
      // get "INVALID_ARGS" back.
      ASSERT_EQ(ZX_ERR_INVALID_ARGS, state.Start(0, StartMode::Saturate));

      // Now go ahead and call start.  This should cause the buffer to become
      // allocated, and for both static and thread names to be reported (static
      // before thread)
      ASSERT_OK(state.Start(kAllGroups, StartMode::Saturate));
      {
        Guard<SpinLock, IrqSave> guard{&state.write_lock_};
        EXPECT_NONNULL(state.buffer_);
        EXPECT_GT(state.bufsize_, 0u);
        EXPECT_LE(state.bufsize_, state.target_bufsize_);
        EXPECT_EQ(kDefaultBufferSize, state.target_bufsize_);
        EXPECT_EQ(1u, state.static_name_report_count_);
        EXPECT_EQ(1u, state.thread_name_report_count_);
        EXPECT_LE(state.last_static_name_report_time_, state.last_thread_name_report_time_);
        EXPECT_EQ(KTRACE_GRP_TO_MASK(kAllGroups), state.grpmask());
      }
    }

    {
      // Perform a similar test, but this time passing a non-zero group mask to
      // init.  This should cause tracing to go live immediately.
      TestKTraceState state;
      ASSERT_TRUE(state.Init(kDefaultBufferSize, kAllGroups));

      {
        Guard<SpinLock, IrqSave> guard{&state.write_lock_};
        EXPECT_NONNULL(state.buffer_);
        EXPECT_GT(state.bufsize_, 0u);
        EXPECT_LE(state.bufsize_, state.target_bufsize_);
        EXPECT_EQ(kDefaultBufferSize, state.target_bufsize_);
        EXPECT_EQ(1u, state.static_name_report_count_);
        EXPECT_EQ(1u, state.thread_name_report_count_);
        EXPECT_LE(state.last_static_name_report_time_, state.last_thread_name_report_time_);
        EXPECT_EQ(KTRACE_GRP_TO_MASK(kAllGroups), state.grpmask());
      }
    }

    {
      // Initialize a trace, then start it in circular mode.
      TestKTraceState state;
      ASSERT_TRUE(state.Init(kDefaultBufferSize, 0));
      ASSERT_OK(state.Start(kAllGroups, StartMode::Circular));

      // Stopping and starting the trace again in circular mode should be OK.
      ASSERT_OK(state.Stop());
      ASSERT_OK(state.Start(kAllGroups, StartMode::Circular));

      // Stopping and starting the trace again in saturate mode should be an
      // error.
      ASSERT_OK(state.Stop());
      ASSERT_EQ(ZX_ERR_BAD_STATE, state.Start(kAllGroups, StartMode::Saturate));

      // Rewinding the buffer should fix the issue.
      // error.
      ASSERT_OK(state.Rewind());
      ASSERT_OK(state.Start(kAllGroups, StartMode::Saturate));
    }

    END_TEST;
  }

  static bool NamesTest() {
    BEGIN_TEST;
    // Create a small trace buffer and initialize it.
    constexpr uint32_t kGroups = 0x3;
    TestKTraceState state;
    ASSERT_TRUE(state.Init(kDefaultBufferSize, kGroups));

    // Immediately after initialization, ktrace will write two metadata records
    // expressing the version of the trace buffer format, as well as the
    // resolution of the timestamps in the trace.  Make sure that the offset
    // reflects this.
    uint32_t expected_offset = sizeof(ktrace_rec_32b_t) * 2;
    ASSERT_TRUE(state.CheckExpectedOffset(expected_offset));

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
      size_t string_storage = ktl::min(strlen(vec.name) + 1, ZX_MAX_NAME_LEN);

      // Total storage is the storage for the name header, plus the string
      // storage, all rounded up to the nearest 8 bytes.
      return (KTRACE_NAMESIZE + static_cast<uint32_t>(string_storage) + 7) & ~0x7;
    };

    // Add all of the name test vectors to the trace buffer.  Verify that the
    // buffer grows as we would expect while we do so.
    uint32_t expected_present_count = 0;
    for (const auto& vec : kTestVectors) {
      ASSERT_TRUE(state.CheckExpectedOffset(expected_offset));

      state.WriteNameEtc(vec.tag, vec.id, vec.arg, vec.name, vec.always);
      if (vec.expected_present) {
        expected_offset += ExpectedNameRecordSize(vec);
        ++expected_present_count;
      }

      ASSERT_TRUE(state.CheckExpectedOffset(expected_offset));
    }

    // Now, stop the trace, enumerate the buffer, and make sure that the records
    // we expect are present.
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
      const size_t expected_name_len = ktl::min(strlen(vec.name), ZX_MAX_NAME_LEN - 1);
      ASSERT_EQ(expected_name_len, strlen(rec.name));
      EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>(vec.name),
                      reinterpret_cast<const uint8_t*>(rec.name), expected_name_len);
      EXPECT_EQ(0, rec.name[expected_name_len]);

      ++vec_id;
      END_TEST;
    };

    ASSERT_OK(state.Stop());
    ASSERT_TRUE(state.TestAllRecords(records_enumerated, checker));
    EXPECT_EQ(expected_present_count, records_enumerated);

    END_TEST;
  }

  static bool WriteRecordsTest() {
    BEGIN_TEST;

    // Create a small trace buffer and initialize it.
    constexpr uint32_t kGroups = 0x3;
    TestKTraceState state;
    ASSERT_TRUE(state.Init(kDefaultBufferSize, kGroups));

    uint32_t expected_offset = sizeof(ktrace_rec_32b_t) * 2;
    ASSERT_TRUE(state.CheckExpectedOffset(expected_offset));

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

    // Now, stop the trace and read the records back out and verify their contents.
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

    ASSERT_OK(state.Stop());
    ASSERT_TRUE(state.TestAllRecords(records_enumerated, checker));
    EXPECT_EQ(7u, records_enumerated);

    END_TEST;
  }

  static bool SaturationTest() {
    BEGIN_TEST;

    // Create a small trace buffer and initialize it.
    constexpr uint32_t kGroups = KTRACE_GRP_PROBE;
    TestKTraceState state;
    ASSERT_TRUE(state.Init(kDefaultBufferSize, kGroups));

    // Write the (max - 1) 32 byte records to the buffer, then write a single 24 byte record.
    for (uint32_t i = 0; i < kMax32bRecords - 1; ++i) {
      state.WriteRecord(TAG_PROBE_32(1), kRecordCurrentTimestamp, 5u, 6u, 7u, 8u);
    }
    state.WriteRecord(TAG_PROBE_24(1), kRecordCurrentTimestamp, 5u, 6u);

    // The buffer will not think that it is full just yet.
    uint32_t rcnt = 0;
    auto checker = [&](const ktrace_header_t* hdr) -> bool { return true; };
    EXPECT_EQ(KTRACE_GRP_TO_MASK(kGroups), state.grpmask());
    ASSERT_OK(state.Stop());
    EXPECT_TRUE(state.TestAllRecords(rcnt, checker));
    EXPECT_TRUE(state.CheckExpectedOffset(kDefaultBufferSize - 8));
    EXPECT_EQ(kMax32bRecords, rcnt);

    // Now write one more record, this time with a different payload.
    ASSERT_OK(state.Start(kGroups, internal::KTraceState::StartMode::Saturate));
    state.WriteRecord(TAG_PROBE_32(1), kRecordCurrentTimestamp, 0xbaadf00d, 0xf00dbaad, ~0xbaadf00d,
                      ~0xf00dbaad);

    // The buffer should now think that it is full (the group mask will be
    // cleared), and all of the original records should be present (but not the
    // new one).
    EXPECT_EQ(0u, state.grpmask());
    ASSERT_OK(state.Stop());

    auto payload_checker = [&](const ktrace_header_t* hdr) -> bool {
      BEGIN_TEST;
      ASSERT_NONNULL(hdr);

      auto payload = reinterpret_cast<const uint32_t*>(hdr + 1);
      const uint32_t len = KTRACE_LEN(hdr->tag);
      switch (len) {
        case 32:
          EXPECT_EQ(7u, payload[2]);
          EXPECT_EQ(8u, payload[3]);
          __FALLTHROUGH;
        case 24:
          EXPECT_EQ(5u, payload[0]);
          EXPECT_EQ(6u, payload[1]);
          break;
        default:
          EXPECT_EQ(32u, len);
          break;
      }

      END_TEST;
    };
    EXPECT_TRUE(state.TestAllRecords(rcnt, payload_checker));
    EXPECT_EQ(kMax32bRecords, rcnt);

    END_TEST;
  }

  static bool RewindTest() {
    BEGIN_TEST;

    // Create a small trace buffer and initialize it.
    constexpr uint32_t kGroups = KTRACE_GRP_PROBE;
    TestKTraceState state;
    ASSERT_TRUE(state.Init(kDefaultBufferSize, kGroups));

    uint32_t expected_offset = sizeof(ktrace_rec_32b_t) * 2;
    ASSERT_TRUE(state.CheckExpectedOffset(expected_offset));

    // Write a couple of records.
    state.WriteRecord(TAG_PROBE_32(1), kRecordCurrentTimestamp, 5u, 6u);
    state.WriteRecord(TAG_PROBE_32(1), kRecordCurrentTimestamp, 5u, 6u);

    // The offset should have moved, and the number of records in the buffer should now be 2.
    uint32_t rcnt = 0;
    auto checker = [&](const ktrace_header_t* hdr) -> bool { return true; };
    EXPECT_TRUE(state.CheckExpectedOffset(expected_offset, CheckOp::LT));
    EXPECT_EQ(KTRACE_GRP_TO_MASK(kGroups), state.grpmask());
    ASSERT_OK(state.Stop());
    EXPECT_TRUE(state.TestAllRecords(rcnt, checker));
    EXPECT_EQ(2u, rcnt);

    // Rewind.  The offset should return to the beginning, and there
    // should be no records in the buffer.
    ASSERT_OK(state.Rewind());
    EXPECT_TRUE(state.CheckExpectedOffset(expected_offset));
    EXPECT_EQ(0u, state.grpmask());
    EXPECT_TRUE(state.TestAllRecords(rcnt, checker));
    EXPECT_EQ(0u, rcnt);

    // Start again, and this time saturate the buffer
    ASSERT_OK(state.Start(kGroups, StartMode::Saturate));
    for (uint32_t i = 0; i < kMax32bRecords + 10; ++i) {
      state.WriteRecord(TAG_PROBE_32(1), kRecordCurrentTimestamp, 5u, 6u, 7u, 8u);
    }
    EXPECT_EQ(0u, state.grpmask());
    ASSERT_OK(state.Stop());
    EXPECT_TRUE(state.TestAllRecords(rcnt, checker));
    EXPECT_EQ(kMax32bRecords, rcnt);

    // Finally, rewind again.  The offset should return to the
    // beginning, and there should be no records in the buffer.
    ASSERT_OK(state.Rewind());
    EXPECT_TRUE(state.CheckExpectedOffset(expected_offset));
    EXPECT_EQ(0u, state.grpmask());
    EXPECT_TRUE(state.TestAllRecords(rcnt, checker));
    EXPECT_EQ(0u, rcnt);

    END_TEST;
  }

  static bool StateCheckTest() {
    BEGIN_TEST;

    constexpr uint32_t kAllGroups = KTRACE_GRP_ALL;
    constexpr uint32_t kSomeGroups = 0x3;

    {
      TestKTraceState state;
      ASSERT_TRUE(state.Init(kDefaultBufferSize, 0));

      // We didn't provide a non-zero initial set of groups, so the trace should
      // not be started right now.  Stopping, rewinding, and reading are all
      // legal (although, stopping does nothing).  We have not allocated our
      // buffer yet, so not even the static metadata should be available to
      // read.
      ASSERT_OK(state.Stop());
      ASSERT_EQ(0u, state.ReadUser(user_out_ptr<void>(nullptr), 0, 0));
      ASSERT_OK(state.Rewind());
      ASSERT_EQ(KTRACE_GRP_TO_MASK(0u), state.grpmask());

      // Starting should succeed.
      ASSERT_OK(state.Start(kAllGroups, StartMode::Saturate));
      ASSERT_EQ(KTRACE_GRP_TO_MASK(kAllGroups), state.grpmask());

      // Now that we are started, rewinding or should fail because of the state
      // check.
      ASSERT_EQ(ZX_ERR_BAD_STATE, state.Rewind());
      ASSERT_EQ(ZX_ERR_BAD_STATE, state.ReadUser(user_out_ptr<void>(nullptr), 0, 0));
      ASSERT_EQ(KTRACE_GRP_TO_MASK(kAllGroups), state.grpmask());

      // Starting while already started should succeed, but change the active
      // group mask.
      ASSERT_OK(state.Start(kSomeGroups, StartMode::Saturate));
      ASSERT_EQ(KTRACE_GRP_TO_MASK(kSomeGroups), state.grpmask());

      // Stopping is still OK, and actually does something now (it clears the
      // group mask).
      ASSERT_OK(state.Stop());
      ASSERT_EQ(KTRACE_GRP_TO_MASK(0u), state.grpmask());

      // Now that we are stopped, we can read, rewind, and stop again.  Since we
      // have started before, we expect that the amount of data available to
      // read should be equal to the size of the two static metadata records.
      const ssize_t expected_size = sizeof(ktrace_rec_32b_t) * 2;
      ASSERT_EQ(expected_size, state.ReadUser(user_out_ptr<void>(nullptr), 0, 0));
      ASSERT_OK(state.Rewind());
    }

    {
      // Same checks as before, but this time start in the started state after
      // init by providing a non-zero set of groups.
      TestKTraceState state;
      ASSERT_TRUE(state.Init(kDefaultBufferSize, kAllGroups));
      ASSERT_EQ(KTRACE_GRP_TO_MASK(kAllGroups), state.grpmask());

      // We are started, so rewinding or reading should fail because of the
      // state check.
      ASSERT_EQ(ZX_ERR_BAD_STATE, state.Rewind());
      ASSERT_EQ(ZX_ERR_BAD_STATE, state.ReadUser(user_out_ptr<void>(nullptr), 0, 0));
      ASSERT_EQ(KTRACE_GRP_TO_MASK(kAllGroups), state.grpmask());

      // "Restarting" should change the active group mask.
      ASSERT_OK(state.Start(kSomeGroups, StartMode::Saturate));
      ASSERT_EQ(KTRACE_GRP_TO_MASK(kSomeGroups), state.grpmask());

      // Stopping should work.
      ASSERT_OK(state.Stop());
      ASSERT_EQ(KTRACE_GRP_TO_MASK(0u), state.grpmask());

      // Stopping again, rewinding, and reading are all OK now.
      const ssize_t expected_size = sizeof(ktrace_rec_32b_t) * 2;
      ASSERT_OK(state.Stop());
      ASSERT_EQ(expected_size, state.ReadUser(user_out_ptr<void>(nullptr), 0, 0));
      ASSERT_OK(state.Rewind());
      ASSERT_OK(state.Stop());
    }

    END_TEST;
  }

  static bool CircularWriteTest() {
    BEGIN_TEST;

    enum class Padding { Needed, NotNeeded };
    constexpr uint32_t kAllGroups = KTRACE_GRP_ALL;
    constexpr ktl::array kPasses = {Padding::Needed, Padding::NotNeeded};

    // Define a couple of lambdas and small amount of state which we will be
    // using to verify the contents of the trace buffer after writing different
    // patterns to it.
    //
    // The test will be conducted in two passes; one pass where we fill up our
    // buffer perfectly when we wrap, and another where we do not (and need a
    // padding record).  In both passes, we start by writing two records while
    // we are in "saturation" mode.  These records are part of the "static"
    // region of the trace and should always be present.  These static records
    // also have distinct patterns that we can look for when verifying the
    // contents of the trace.
    //
    // After writing these records and verifying the contents of the buffer, we
    // restart in circular mode and write just enough in the way of records to
    // fill the trace buffer, but not wrap it.  These new "circular" records
    // contain distinct payloads based on the order in which they were written
    // and can be used for verification.  In addition, we expect a padding
    // record to be present during the padding pass, but _only_ once and _only_
    // after we have forced the buffer to wrap.
    uint32_t record_count = 0;
    uint32_t expected_first_circular = 0;
    uint32_t enumerated_records = 0;
    bool saw_padding = false;

    auto ResetPayloadCheckerState = [&](uint32_t first_circular) {
      record_count = 0;
      expected_first_circular = first_circular;
      enumerated_records = 0;
      saw_padding = false;
    };

    for (const Padding pass : kPasses) {
      auto payload_checker = [&](const ktrace_header_t* hdr) {
        BEGIN_TEST;

        ASSERT_NONNULL(hdr);
        auto payload = reinterpret_cast<const uint32_t*>(hdr + 1);
        const uint32_t len = KTRACE_LEN(hdr->tag);

        if (record_count == 0) {
          // Record number #0 should always be present, always be 32 bytes
          // long, and always have the 0xAAAAAAAA, 0, 0, 0 payload.
          ASSERT_EQ(32u, len);
          EXPECT_EQ(0xaaaaaaaa, payload[0]);
          EXPECT_EQ(0u, payload[1]);
          EXPECT_EQ(0u, payload[2]);
          EXPECT_EQ(0u, payload[3]);
        } else if (record_count == 1) {
          // Record number #1 should always be present, and will have a length
          // of 24 or 32 bytes, and a 0xbbbbbbbb or 0xcccccccc payload,
          // depending on whether or not this pass of the test is one where we
          // expect to need a padding record or not.
          // long, and always have the 0xAAAAAAAA, 0, 0, 0 payload.
          if (pass == Padding::Needed) {
            ASSERT_EQ(24u, len);
            EXPECT_EQ(0xbbbbbbbb, payload[0]);
            EXPECT_EQ(0u, payload[1]);
          } else {
            ASSERT_EQ(32u, len);
            EXPECT_EQ(0xcccccccc, payload[0]);
            EXPECT_EQ(0u, payload[1]);
            EXPECT_EQ(0u, payload[2]);
            EXPECT_EQ(0u, payload[3]);
          }
        } else {
          // All subsequent records should either be a padding record, or a 32
          // byte record whose payload values are a function of their index.
          if (KTRACE_GROUP(hdr->tag) != 0) {
            // A non-zero group indicates a normal record.
            const uint32_t ndx = record_count + expected_first_circular - 2;
            ASSERT_EQ(32u, len);
            EXPECT_EQ(ndx + 0, payload[0]);
            EXPECT_EQ(ndx + 1, payload[1]);
            EXPECT_EQ(ndx + 2, payload[2]);
            EXPECT_EQ(ndx + 3, payload[3]);
          } else {
            // A group of 0 indicates a padding record.
            if (pass == Padding::Needed) {
              // should only ever see (at most) a single padding record per check.
              ASSERT_FALSE(saw_padding);
              saw_padding = true;
            } else {
              ASSERT_TRUE(false);  // we should not be seeing padding on a non-padding pass.
            }

            // Don't count padding records in the record count.
            --record_count;
          }
        }

        ++record_count;
        END_TEST;
      };

      // Allocate our trace buffer and auto-start it during init in non-circular
      // mode.
      TestKTraceState state;
      ASSERT_TRUE(state.Init(kDefaultBufferSize, kAllGroups));

      // In order to run this test, we need enough space in our buffer after the
      // 2 reserved metadata records for a least two "static" records, and a
      // small number of extra records.
      constexpr uint32_t kOverhead = sizeof(ktrace_rec_32b_t) * 2;
      constexpr uint32_t kExtraRecords = 5;
      const uint32_t kStaticOverhead = 32 + ((pass == Padding::Needed) ? 24 : 32);
      ASSERT_GE(kDefaultBufferSize, (kOverhead + kStaticOverhead + (32 * kExtraRecords)));

      state.WriteRecord(TAG_PROBE_32(1), kRecordCurrentTimestamp, 0xaaaaaaaa, 0u, 0u, 0u);
      if (pass == Padding::Needed) {
        ASSERT_NE(0u, (kDefaultBufferSize - (kOverhead + kStaticOverhead)) % 32);
        state.WriteRecord(TAG_PROBE_24(1), kRecordCurrentTimestamp, 0xbbbbbbbb, 0u);
      } else {
        ASSERT_EQ(0u, (kDefaultBufferSize - (kOverhead + kStaticOverhead)) % 32);
        state.WriteRecord(TAG_PROBE_32(1), kRecordCurrentTimestamp, 0xcccccccc, 0u, 0u, 0u);
      }
      ASSERT_TRUE(state.CheckExpectedOffset(kOverhead + kStaticOverhead));

      // Stop the trace and verify that we have the records we expect.  Right
      // now, we expect to find only a pair of static records after the
      // metadata.
      const uint32_t kMaxCircular32bRecords =
          (kDefaultBufferSize - (kOverhead + kStaticOverhead)) / 32u;
      ASSERT_OK(state.Stop());
      ResetPayloadCheckerState(0);
      EXPECT_TRUE(state.TestAllRecords(enumerated_records, payload_checker));
      EXPECT_EQ(2u, enumerated_records);
      EXPECT_FALSE(saw_padding);

      // OK, now restart in circular mode, and write the maximum number of 32
      // byte records we can, without causing a wrap.
      ASSERT_OK(state.Start(kAllGroups, StartMode::Circular));
      for (uint32_t i = 0; i < kMaxCircular32bRecords; ++i) {
        const uint32_t ndx = i;
        state.WriteRecord(TAG_PROBE_32(1), kRecordCurrentTimestamp, ndx + 0, ndx + 1, ndx + 2,
                          ndx + 3);
      }

      // Stop, and check the contents.
      ASSERT_OK(state.Stop());
      ResetPayloadCheckerState(0);
      EXPECT_TRUE(state.TestAllRecords(enumerated_records, payload_checker));
      EXPECT_EQ(2u + kMaxCircular32bRecords, enumerated_records);
      EXPECT_FALSE(saw_padding);

      // Start one last time, writing our extra records.  This should cause the
      // circular section of the ktrace buffer to wrap, requiring a padding
      // record if (and only if) this is the padding pass.  Our first "circular"
      // record should start with a payload index equal to the number of extra
      // records we wrote.
      ASSERT_OK(state.Start(kAllGroups, StartMode::Circular));
      for (uint32_t i = 0; i < kExtraRecords; ++i) {
        const uint32_t ndx = i + kMaxCircular32bRecords;
        state.WriteRecord(TAG_PROBE_32(1), kRecordCurrentTimestamp, ndx + 0, ndx + 1, ndx + 2,
                          ndx + 3);
      }

      // Stop, and check the contents.
      ASSERT_OK(state.Stop());
      ResetPayloadCheckerState(kExtraRecords);
      EXPECT_TRUE(state.TestAllRecords(enumerated_records, payload_checker));
      if (pass == Padding::Needed) {
        EXPECT_EQ(2u + kMaxCircular32bRecords + 1u, enumerated_records);
        EXPECT_TRUE(saw_padding);
      } else {
        EXPECT_EQ(2u + kMaxCircular32bRecords, enumerated_records);
        EXPECT_FALSE(saw_padding);
      }
    }

    END_TEST;
  }

  static bool FxtCompatWriterTest() {
    BEGIN_TEST;

    constexpr uint32_t kAllGroups = KTRACE_GRP_ALL;

    // Create a small trace buffer and initialize it.
    TestKTraceState state;
    ASSERT_TRUE(state.Init(kDefaultBufferSize, kAllGroups));

    // Immediately after initialization, ktrace will write two metadata records
    // expressing the version of the trace buffer format, as well as the
    // resolution of the timestamps in the trace.  Make sure that the offset
    // reflects this.
    uint32_t expected_offset = sizeof(ktrace_rec_32b_t) * 2;
    ASSERT_TRUE(state.CheckExpectedOffset(expected_offset));

    // This test works with the FxtCompatWriter and Reservation objects directly
    // rather than using the libfxt functions. Here we build a valid string
    // record in a convoluted way to cover various methods that libfxt uses to
    // write bytes.
    constexpr uint64_t fxt_header = 0x0000'0026'0001'0062;

    auto wrapper = state.make_fxt_writer(KTRACE_TAG(0x1, 0x1, 0));
    auto reservation = wrapper.Reserve(fxt_header);
    ASSERT_OK(reservation.status_value());
    reservation->WriteWord(0x6867'6665'6463'6261);
    reservation->WriteBytes("0123456789ABCDEF", 16);
    reservation->WriteBytes("remaining data", 14);
    reservation->Commit();

    auto record_checker = [&](const ktrace_header_t* hdr) {
      BEGIN_TEST;

      ASSERT_NONNULL(hdr);

      EXPECT_EQ(KTRACE_GROUP(hdr->tag), 0x1u | KTRACE_GRP_FXT);
      // KTrace length field should be computed from the FXT header.
      ASSERT_EQ(KTRACE_LEN(hdr->tag), 7 * sizeof(uint64_t));

      const char* fxt_start = reinterpret_cast<const char*>(hdr) + sizeof(uint64_t);

      EXPECT_EQ(0, memcmp(fxt_start,
                          "\x62\x00\x01\x00\x26\x00\x00\x00"
                          "abcdefgh"
                          "01234567"
                          "89ABCDEF"
                          "remainin"
                          "g data\0\0",
                          6 * sizeof(uint64_t)));

      END_TEST;
    };

    ASSERT_OK(state.Stop());

    uint32_t enumerated_records = 0;
    state.TestAllRecords(enumerated_records, record_checker);
    EXPECT_EQ(1u, enumerated_records);

    END_TEST;
  }

 private:
  //////////////////////////////////////////////////////////////////////////////
  //
  // Actual class implementation starts here.  All members are private, the only
  // public members of the class are the static test hooks.
  //
  //////////////////////////////////////////////////////////////////////////////

  // clang-format off
  enum class CheckOp { LT, LE, EQ, GT, GE };
  // clang-format on

  TestKTraceState() {
    // disable diagnostic printfs in the test instances of KTrace we create.
    disable_diags_printfs_ = true;
  }

  // TODO(johngro): The default KTraceState implementation never cleans up its
  // buffer allocation, as it assumes that it is being used as a global
  // singleton, and that the kernel will never "exit". Test instances of
  // KTraceState *must* clean themselves up, however.  Should we push this
  // behavior down one level into the default KTraceState implementation's
  // destructor, even though it (currently) does not ever have any reason to
  // destruct?
  ~TestKTraceState() {
    if (buffer_ != nullptr) {
      VmAspace* aspace = VmAspace::kernel_aspace();
      aspace->FreeRegion(reinterpret_cast<vaddr_t>(buffer_));
    }
  }

  // We interpose ourselves in the Init path so that we can allocate the side
  // buffer we will use for validation.
  [[nodiscard]] bool Init(uint32_t target_bufsize, uint32_t initial_groups) {
    BEGIN_TEST;

    // Tests should always be allocating in units of page size.
    ASSERT_EQ(0u, target_bufsize & (PAGE_SIZE - 1));
    // Double init is not allowed.
    ASSERT_NULL(validation_buffer_.get());

    fbl::AllocChecker ac;
    validation_buffer_.reset(new (&ac) uint8_t[target_bufsize]);
    ASSERT_TRUE(ac.check());
    validation_buffer_size_ = target_bufsize;

    KTraceState::Init(target_bufsize, initial_groups);

    // Make sure that the buffer size we requested was allocated exactly.
    {
      Guard<SpinLock, IrqSave> guard{&write_lock_};
      ASSERT_GE(target_bufsize, bufsize_);
    }

    END_TEST;
  }

  void ReportStaticNames() override {
    last_static_name_report_time_ = current_time();
    ++static_name_report_count_;
  }

  void ReportThreadProcessNames() override {
    last_thread_name_report_time_ = current_time();
    ++thread_name_report_count_;
  }

  zx_status_t CopyToUser(user_out_ptr<uint8_t> dst, const uint8_t* src, size_t len) override {
    ::memcpy(dst.get(), src, len);
    return ZX_OK;
  }

  // Check to make sure that the buffer is not operating in circular mode, and
  // that the write pointer is at the offset we expect.
  [[nodiscard]] bool CheckExpectedOffset(size_t expected, CheckOp op = CheckOp::EQ)
      TA_EXCL(write_lock_) {
    BEGIN_TEST;

    Guard<SpinLock, IrqSave> guard{&write_lock_};
    switch (op) {
      // clang-format off
      case CheckOp::LT: EXPECT_LT(expected, wr_); break;
      case CheckOp::LE: EXPECT_LE(expected, wr_); break;
      case CheckOp::EQ: EXPECT_EQ(expected, wr_); break;
      case CheckOp::GT: EXPECT_GT(expected, wr_); break;
      case CheckOp::GE: EXPECT_GE(expected, wr_); break;
      // clang-format on
      default:
        ASSERT_TRUE(false);
    }
    EXPECT_EQ(0u, rd_);
    EXPECT_EQ(0u, circular_size_);

    END_TEST;
  }

  template <typename Checker>
  bool TestAllRecords(uint32_t& records_enumerated_out, const Checker& do_check)
      TA_EXCL(write_lock_) {
    BEGIN_TEST;

    // Make sure that we give a value to all of our out parameters.
    records_enumerated_out = 0;

    // Make sure that Read reports a reasonable size needed to read the buffer.
    ssize_t available = ReadUser(user_out_ptr<void>(nullptr), 0, 0);
    ASSERT_GE(available, 0);
    ASSERT_LE(static_cast<size_t>(available), validation_buffer_size_);

    // Now actually read the data, make sure that we read the same amount that
    // the size operation told us we would need to read.
    ASSERT_NONNULL(validation_buffer_.get());
    size_t to_validate =
        ReadUser(user_out_ptr<void>(validation_buffer_.get()), 0, validation_buffer_size_);
    ASSERT_EQ(static_cast<size_t>(available), to_validate);

    uint32_t rd_offset = sizeof(ktrace_rec_32b_t) * 2;
    ASSERT_GE(to_validate, rd_offset);

    // We expect all trace buffers to start with a metadata records indicating the
    // version of the trace buffer format, and the clock resolution.  Verify that
    // these are present.
    const uint8_t* buffer = validation_buffer_.get();
    const ktrace_rec_32b_t& version_rec = reinterpret_cast<const ktrace_rec_32b_t*>(buffer)[0];
    const ktrace_rec_32b_t& clock_res_rec = reinterpret_cast<const ktrace_rec_32b_t*>(buffer)[1];

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

    while (rd_offset < to_validate) {
      auto hdr = reinterpret_cast<const ktrace_header_t*>(buffer + rd_offset);
      // Zero length records are not legal.
      ASSERT_GT(KTRACE_LEN(hdr->tag), 0u);

      // Make sure the record matches expectations.
      ASSERT_TRUE(do_check(hdr));

      // Advance to the next record.
      ++records_enumerated_out;
      rd_offset += KTRACE_LEN(hdr->tag);
    }

    EXPECT_EQ(rd_offset, to_validate);

    ReportRecord.cancel();
    END_TEST;
  }

  zx_time_t last_static_name_report_time_{0};
  zx_time_t last_thread_name_report_time_{0};
  uint32_t static_name_report_count_{0};
  uint32_t thread_name_report_count_{0};

  ktl::unique_ptr<uint8_t[]> validation_buffer_;
  size_t validation_buffer_size_{0};
};

}  // namespace ktrace_tests

UNITTEST_START_TESTCASE(ktrace_tests)
UNITTEST("init/start", ktrace_tests::TestKTraceState::InitStartTest)
UNITTEST("names", ktrace_tests::TestKTraceState::NamesTest)
UNITTEST("write record", ktrace_tests::TestKTraceState::WriteRecordsTest)
UNITTEST("saturation", ktrace_tests::TestKTraceState::SaturationTest)
UNITTEST("rewind", ktrace_tests::TestKTraceState::RewindTest)
UNITTEST("state check", ktrace_tests::TestKTraceState::StateCheckTest)
UNITTEST("circular", ktrace_tests::TestKTraceState::CircularWriteTest)
UNITTEST("fxt compat writer", ktrace_tests::TestKTraceState::FxtCompatWriterTest)
UNITTEST_END_TESTCASE(ktrace_tests, "ktrace", "KTrace tests")
