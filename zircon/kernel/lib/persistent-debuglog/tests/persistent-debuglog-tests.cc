// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/lazy_init/lazy_init.h>
#include <lib/persistent-debuglog.h>
#include <lib/unittest/unittest.h>

#include <fbl/alloc_checker.h>
#include <ktl/algorithm.h>
#include <ktl/array.h>
#include <ktl/unique_ptr.h>

#include "../persistent-debuglog-internal.h"

#include <ktl/enforce.h>

namespace tests {
struct PersistentDebuglogTestingFriend {
  using LogHeader = PersistentDebugLog::LogHeader;
  static void ForceReset(PersistentDebugLog& log) { log.ForceReset(); }
};
}  // namespace tests

namespace {

using LogHeader = ::tests::PersistentDebuglogTestingFriend::LogHeader;

struct Buffer {
  bool Setup(uint32_t new_capacity) {
    BEGIN_TEST;

    ASSERT_NULL(storage.get());
    ASSERT_EQ(0u, capacity);
    ASSERT_GT(new_capacity, 0u);

    fbl::AllocChecker ac;
    storage.reset(new (&ac) char[new_capacity]{0});
    ASSERT_TRUE(ac.check());
    capacity = new_capacity;

    END_TEST;
  }

  ktl::unique_ptr<char[]> storage = nullptr;
  uint32_t capacity = 0;
} recovered_log;

struct TestEnvironment {
  bool Setup(uint32_t log_size, uint32_t recovered_size) {
    BEGIN_TEST;

    ASSERT_TRUE(log_buffer.Setup(log_size));
    ASSERT_TRUE(recovered_log_buffer.Setup(recovered_size));
    log.Initialize(recovered_log_buffer.storage.get(), recovered_log_buffer.capacity);
    hdr = reinterpret_cast<LogHeader*>(log_buffer.storage.get());

    END_TEST;
  }

  lazy_init::LazyInit<PersistentDebugLog, lazy_init::CheckType::None,
                      lazy_init::Destructor::Disabled>
      log;
  Buffer log_buffer;
  Buffer recovered_log_buffer;
  LogHeader* hdr = nullptr;
};

bool CheckRecoveredLogIsEmpty(const PersistentDebugLog& log) {
  BEGIN_TEST;

  ktl::string_view sv = log.GetRecoveredLog();
  ASSERT_EQ(0u, sv.size());
  ASSERT_NULL(sv.data());

  END_TEST;
}

template <typename T, size_t N>
bool CheckRecoveredLogMatches(const PersistentDebugLog& log, const ktl::array<T, N>& test_vectors) {
  BEGIN_TEST;
  static_assert(ktl::is_same_v<T, ktl::string_view>);

  // Check that our string view representation of the recovered log matches the
  // test vectors.
  ktl::string_view recovered = log.GetRecoveredLog();
  size_t offset = 0;
  for (const auto& test_vector : test_vectors) {
    size_t log_amt = recovered.size() - offset;
    ASSERT_GE(log_amt, test_vector.size());
    ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(test_vector.data()),
                    reinterpret_cast<const uint8_t*>(recovered.data()) + offset,
                    test_vector.size());
    offset += test_vector.size();
  }
  ASSERT_EQ(recovered.size(), offset);

  END_TEST;
}

bool pdlog_basic_test() {
  BEGIN_TEST;

  TestEnvironment env;
  ASSERT_TRUE(env.Setup(128, 128));

  // Our allocated buffer starts filled with zeros.  The log header should
  // consider this to be an invalid value.
  ASSERT_FALSE(env.hdr->IsMagicValid());
  ASSERT_EQ(0u, env.hdr->magic);

  // Set the location of the persistent log ram.  This will attempt to recover
  // the (currently empty) log, and initialize the active log's header in the
  // process.
  env.log->SetLocation(env.log_buffer.storage.get(), env.log_buffer.capacity);
  ASSERT_TRUE(env.hdr->IsMagicValid());
  ASSERT_EQ(0u, env.hdr->rd_ptr);

  // The recovered log should be empty.
  ASSERT_TRUE(CheckRecoveredLogIsEmpty(env.log.Get()));

  // Perform some writes to the log
  constexpr ktl::array kTestStrings = {
      "Test pattern 1\n"sv,
      "This has no newline"sv,
      "ABCDEF0123456789\n"sv,
      "Foo Bar Baz\n"sv,
  };

  size_t expected_rd_ptr = 0;
  for (const auto& sv : kTestStrings) {
    env.log->Write(sv);
    expected_rd_ptr += sv.length();
  }

  // The recovered log should be empty.
  ASSERT_TRUE(CheckRecoveredLogIsEmpty(env.log.Get()));

  // But the read pointer should have advanced to match the length of the
  // strings we have written so far.
  ASSERT_EQ(expected_rd_ptr, env.hdr->rd_ptr);

  // Simulate a reboot by resetting our debug log, then setting the location of
  // our persistent buffer once again.  This will cause the implementation to
  // attempt to recover our log, at which point in time we can verify that
  // everything recovered correctly.
  tests::PersistentDebuglogTestingFriend::ForceReset(env.log.Get());
  env.log->SetLocation(env.log_buffer.storage.get(), env.log_buffer.capacity);
  ASSERT_TRUE(CheckRecoveredLogMatches(env.log.Get(), kTestStrings));

  END_TEST;
}

bool pdlog_logwrap_test() {
  BEGIN_TEST;

  TestEnvironment env;
  ASSERT_TRUE(env.Setup(128, 128));
  env.log->SetLocation(env.log_buffer.storage.get(), env.log_buffer.capacity);

  // Perform enough writes to the log that it wraps at least once.
  constexpr ktl::string_view test_str{"0123456789AB\n"sv};
  constexpr size_t kRepeatCount = 23;
  for (size_t i = 0; i < kRepeatCount; ++i) {
    env.log->Write(test_str);
  }

  // "reboot" and recover the log.
  tests::PersistentDebuglogTestingFriend::ForceReset(env.log.Get());
  env.log->SetLocation(env.log_buffer.storage.get(), env.log_buffer.capacity);

  // We have 120 bytes worth of payload, and our test string is 13 bytes long
  // and repeated 23 times. So, the total number of bytes we write into the
  // buffer is 299 meaning that we will wrap the buffer two complete times.  In
  // addition, our recovered log will start 120 % 13 == 3 bytes from the end of
  // an instance of our test string.
  static_assert(sizeof(LogHeader) == 8, "Update test vectors to match change in header size!");
  static_assert(test_str.length() == 13);
  constexpr ktl::array kTestStrings = {
      ktl::string_view{test_str.data() + test_str.length() - 3, 3},
      test_str,
      test_str,
      test_str,
      test_str,
      test_str,
      test_str,
      test_str,
      test_str,
      test_str,
  };
  ASSERT_TRUE(CheckRecoveredLogMatches(env.log.Get(), kTestStrings));

  END_TEST;
}

// We expect that all embedded nulls get removed from strings when the log is
// recovered.
bool pdlog_zeros_removed_test() {
  BEGIN_TEST;

  TestEnvironment env;
  ASSERT_TRUE(env.Setup(128, 128));
  env.log->SetLocation(env.log_buffer.storage.get(), env.log_buffer.capacity);

  // Perform some writes to the log which have 0s embedded in them.
  constexpr ktl::array kWithNulls = {
      "This \0has\0nulls\n"sv,
      "\0\0even\0more\0nulls\0\0\0\n"sv,
  };
  for (const auto& sv : kWithNulls) {
    env.log->Write(sv);
  }

  // "reboot" and recover the log.
  tests::PersistentDebuglogTestingFriend::ForceReset(env.log.Get());
  env.log->SetLocation(env.log_buffer.storage.get(), env.log_buffer.capacity);

  // Verify that the nulls are removed during recovery.
  constexpr ktl::array kWithoutNulls = {
      "This hasnulls\n"sv,
      "evenmorenulls\n"sv,
  };
  ASSERT_TRUE(CheckRecoveredLogMatches(env.log.Get(), kWithoutNulls));

  END_TEST;
}

bool pdlog_rejects_bad_magic_test() {
  BEGIN_TEST;

  // Set up a log, put some data into it, then "reboot"
  TestEnvironment env;
  ASSERT_TRUE(env.Setup(128, 128));
  env.log->SetLocation(env.log_buffer.storage.get(), env.log_buffer.capacity);
  env.log->Write("I'm in your base, corrupting your magic numbers!\n"sv);
  tests::PersistentDebuglogTestingFriend::ForceReset(env.log.Get());

  // Before we attempt to recover the log, deliberately corrupt the magic
  // number.
  ++env.hdr->magic;

  // Now attempt to recover the log, and verify that we get nothing.
  env.log->SetLocation(env.log_buffer.storage.get(), env.log_buffer.capacity);
  ASSERT_TRUE(CheckRecoveredLogIsEmpty(env.log.Get()));

  END_TEST;
}

bool pdlog_rejects_bad_rd_ptr_test() {
  BEGIN_TEST;

  // Set up a log, put some data into it, then "reboot"
  TestEnvironment env;
  ASSERT_TRUE(env.Setup(128, 128));
  env.log->SetLocation(env.log_buffer.storage.get(), env.log_buffer.capacity);
  env.log->Write("I'm in your base, corrupting your read pointer!\n"sv);
  tests::PersistentDebuglogTestingFriend::ForceReset(env.log.Get());

  // Before we attempt to recover the log, set the read pointer of the log to
  // something impossible.
  env.hdr->rd_ptr = 0x12356;

  // Now attempt to recover the log, and verify that we get nothing.
  env.log->SetLocation(env.log_buffer.storage.get(), env.log_buffer.capacity);
  ASSERT_TRUE(CheckRecoveredLogIsEmpty(env.log.Get()));

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(persistent_debuglog_tests)
UNITTEST("basic", pdlog_basic_test)
UNITTEST("logwrap", pdlog_logwrap_test)
UNITTEST("zeros_removed", pdlog_zeros_removed_test)
UNITTEST("rejects bad magic", pdlog_rejects_bad_magic_test)
UNITTEST("rejects bad read pointer", pdlog_rejects_bad_rd_ptr_test)
UNITTEST_END_TESTCASE(persistent_debuglog_tests, "pdlog", "Persistent Debuglog Tests")
