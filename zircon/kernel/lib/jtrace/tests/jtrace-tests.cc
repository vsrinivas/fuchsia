// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/jtrace/jtrace.h>
#include <lib/unittest/unittest.h>

#include <fbl/alloc_checker.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <kernel/auto_preempt_disabler.h>
#include <ktl/algorithm.h>
#include <ktl/unique_ptr.h>

#include "../jtrace_internal.h"

#include <ktl/enforce.h>

namespace {

using ::jtrace::IsPersistent;
using ::jtrace::TraceBufferType;
using ::jtrace::UseLargeEntries;

class TestHooks final : public ::jtrace::TraceHooks {
 public:
  void PrintWarning(const char* fmt, ...) final __PRINTFLIKE(2, 3) { ++warning_count_; }
  void PrintInfo(const char* fmt, ...) final __PRINTFLIKE(2, 3) { ++info_count_; }
  void Hexdump(const void* data, size_t size) final { ++hexdump_count_; }
  void PerCpuDumpStarted() { per_cpu_dump_started_ = true; }

  void PrintTraceEntry(const ::jtrace::Entry<UseLargeEntries::Yes>& e, TraceBufferType buf_type,
                       zx_time_t ts, zx_duration_t delta = 0) final {
    InternalPrintTraceEntry(e, buf_type, ts, delta);
  }

  void PrintTraceEntry(const ::jtrace::Entry<UseLargeEntries::No>& e, TraceBufferType buf_type,
                       zx_time_t ts, zx_duration_t delta = 0) final {
    InternalPrintTraceEntry(e, buf_type, ts, delta);
  }

  void Reset(size_t expected_first_id = 0, cpu_num_t expected_per_cpu_num = SMP_MAX_CPUS,
             size_t expected_per_cpu_id = 0) {
    expected_first_id_ = expected_first_id;
    expected_per_cpu_num_ = expected_per_cpu_num;
    expected_per_cpu_id_ = expected_per_cpu_id;
    warning_count_ = 0;
    info_count_ = 0;
    hexdump_count_ = 0;
    entry_count_ = 0;
    validation_failed_ = false;
    per_cpu_dump_started_ = false;
  }

  // When a dump fails, we expect to see exactly one warning, and no info,
  // hexdump, or entry print operations.  The dump should just print a warning
  // explaining why it cannot dump and then exit.
  bool CheckDumpFailed() {
    BEGIN_TEST;
    EXPECT_EQ(1u, warning_count_);
    EXPECT_EQ(0u, info_count_);
    EXPECT_EQ(0u, hexdump_count_);
    EXPECT_EQ(0u, entry_count_);
    EXPECT_FALSE(validation_failed_);
    END_TEST;
  }

  // When a dump succeeds, we should see no warnings or hexdumps, at least one
  // info message (usually more) and the expected number of records dumped.
  bool CheckDumpSucceeded(size_t expected_entry_count) {
    BEGIN_TEST;

    // If we expected to dump per-cpu last entries, and the number of CPUs in
    // the test machine is not equal to SMP_MAX_CPUS, then we expect to see an
    // extra warning during our trace buffer dump telling us that the
    // configuration does not match the actual number of CPUs in the machine.
    const uint32_t kExpectedWarningCount =
        ((expected_per_cpu_num_ != SMP_MAX_CPUS) && (arch_max_num_cpus() != SMP_MAX_CPUS)) ? 1 : 0;

    EXPECT_EQ(kExpectedWarningCount, warning_count_);
    EXPECT_GE(info_count_, 1u);
    EXPECT_EQ(0u, hexdump_count_);
    EXPECT_EQ(expected_entry_count, entry_count_);
    EXPECT_FALSE(validation_failed_);

    if (per_cpu_dump_started_) {
      EXPECT_LT(expected_per_cpu_num_, static_cast<cpu_num_t>(SMP_MAX_CPUS));
    }

    if (expected_per_cpu_num_ < SMP_MAX_CPUS) {
      EXPECT_TRUE(per_cpu_dump_started_);
    }

    END_TEST;
  }

  // When we attempt to dump a corrupt recovered persistent buffer, we should
  // see exactly two warning followed by a hexdump of the corrupted buffer.  The
  // first warning tells the user the reason that the trace is corrupt, while
  // the second is the message just before the hexdump.
  bool CheckDumpCorrupt() {
    BEGIN_TEST;
    EXPECT_EQ(2u, warning_count_);
    EXPECT_EQ(0u, info_count_);
    EXPECT_EQ(1u, hexdump_count_);
    EXPECT_EQ(0u, entry_count_);
    EXPECT_FALSE(validation_failed_);
    END_TEST;
  }

  // When a persistent trace recovers no data (because the header was clean), we
  // expect to see a single info message informing the user that the log was
  // clean, and nothing else.
  bool CheckNothingRecovered() {
    BEGIN_TEST;
    EXPECT_EQ(0u, warning_count_);
    EXPECT_EQ(1u, info_count_);
    EXPECT_EQ(0u, hexdump_count_);
    EXPECT_EQ(0u, entry_count_);
    EXPECT_FALSE(validation_failed_);
    END_TEST;
  }

 private:
  template <UseLargeEntries kUseLargeEntries>
  bool ValidateEntryId(const jtrace::Entry<kUseLargeEntries>& e, size_t expected_id) {
    BEGIN_TEST;

    if (per_cpu_dump_started_ == true) {
      if (e.tag == nullptr) {
        if constexpr (kUseLargeEntries == UseLargeEntries::Yes) {
          EXPECT_NULL(e.ffl_info);
        }
      } else {
        EXPECT_EQ(expected_per_cpu_num_, e.cpu_id);
        if constexpr (kUseLargeEntries == UseLargeEntries::Yes) {
          EXPECT_NONNULL(e.ffl_info);
        }
      }
    }

    if ((per_cpu_dump_started_ == false) ||
        ((e.tag != nullptr) && (e.cpu_id == expected_per_cpu_num_))) {
      ASSERT_EQ(expected_id, e.a);
      if constexpr (kUseLargeEntries == UseLargeEntries::Yes) {
        ASSERT_EQ(expected_id + 1, e.b);
        ASSERT_EQ(expected_id + 2, e.c);
        ASSERT_EQ(expected_id + 3, e.d);
        ASSERT_EQ(expected_id + 4, e.e);
        ASSERT_EQ(expected_id + 5, e.f);
      }
    }

    END_TEST;
  }

  template <UseLargeEntries kUseLargeEntries>
  void InternalPrintTraceEntry(const jtrace::Entry<kUseLargeEntries>& e, TraceBufferType buf_type,
                               zx_time_t ts, zx_duration_t delta = 0) {
    size_t expected_id =
        per_cpu_dump_started_ ? expected_per_cpu_id_ : expected_first_id_ + entry_count_++;
    validation_failed_ = !ValidateEntryId(e, expected_id) || validation_failed_;
  }

  size_t expected_first_id_{0};
  cpu_num_t expected_per_cpu_num_{SMP_MAX_CPUS};
  size_t expected_per_cpu_id_{0};

  size_t warning_count_{0};
  size_t info_count_{0};
  size_t hexdump_count_{0};
  size_t entry_count_{0};
  bool validation_failed_{false};
  bool per_cpu_dump_started_{false};
};

template <typename Config>
struct TestState {
  using Header = typename jtrace::JTrace<Config>::Header;
  using Entry = typename jtrace::JTrace<Config>::Entry;

  TestHooks hooks;
  jtrace::JTrace<Config> trace{hooks};
  alignas(Header) uint8_t trace_storage[Config::kTargetBufferSize]{0};
  uint8_t recovery_template[Config::kTargetBufferSize]{0};

  static constexpr size_t kExpectedEntries =
      (sizeof(trace_storage) - sizeof(Header)) / sizeof(Entry);

  void ResetTrace() {
    // Reset our JTrace instance by manually destroying and reconstructing it
    // using placement new.
    trace.~JTrace();
    new (&trace) jtrace::JTrace<Config>(hooks);
  }
};

template <typename Config>
struct TestEntry : public ::jtrace::Entry<Config::kUseLargeEntries> {
  static constexpr ::jtrace::internal::FileFuncLineInfo kFflInfo = {
      .file = __FILE__,
      .func = "<no function>",
      .line = __LINE__,
  };

  TestEntry(const char* tag, uint32_t val)
      : ::jtrace::Entry<Config::kUseLargeEntries>(tag, &kFflInfo, val) {
    if constexpr (Config::kUseLargeEntries == UseLargeEntries::Yes) {
      this->b = val + 1;
      this->c = val + 2;
      this->d = val + 3;
      this->e = val + 4;
      this->f = val + 5;
    }
  }
};

using CfgLargeEntries = jtrace::Config<1024, 0, IsPersistent::No, UseLargeEntries::Yes>;
using CfgSmallEntries = jtrace::Config<1024, 0, IsPersistent::No, UseLargeEntries::No>;
using CfgPersistLargeEntries = jtrace::Config<1024, 0, IsPersistent::Yes, UseLargeEntries::Yes>;
using CfgPersistSmallEntries = jtrace::Config<1024, 0, IsPersistent::Yes, UseLargeEntries::No>;

}  // namespace

// Introduce a version JTRACE macro, with a couple of small tweaks to allow us
// to target the trace instance under test, and which generates a simple value
// pattern that we use when verifying the trace dump.
#undef JTRACE
#define JTRACE(tgt, tag, val)          \
  do {                                 \
    TestEntry<Config> entry{tag, val}; \
    (tgt).Log(entry);                  \
  } while (false)

// A few of macros which create instances of large and small entries in a way
// similar to how the JTRACE macro does.  Used in the tests::entries test.
static constexpr const char* EXPECTED_MAKE_ENTRY_FUNCTION = "make_entry_function()";
#define MAKE_ENTRY(flavor, tag, ...)                                               \
  [&]() -> auto{                                                                   \
    static constexpr ::jtrace::internal::FileFuncLineInfo ffl_info = {             \
        .file = __FILE__, .func = EXPECTED_MAKE_ENTRY_FUNCTION, .line = __LINE__}; \
    return ::jtrace::Entry<flavor>{tag, &ffl_info, ##__VA_ARGS__};                 \
  }                                                                                \
  ()
#define LARGE_ENTRY(tag, ...) MAKE_ENTRY(::jtrace::UseLargeEntries::Yes, tag, ##__VA_ARGS__)
#define SMALL_ENTRY(tag, ...) MAKE_ENTRY(::jtrace::UseLargeEntries::No, tag, ##__VA_ARGS__)

namespace jtrace {

struct tests {
  static bool entries() {
    BEGIN_TEST;

    enum class enum_default { val = 1 };
    enum class enum_u8 : uint8_t { val = 1 };
    enum class enum_u16 : uint16_t { val = 1 };
    enum class enum_u32 : uint32_t { val = 1 };
    enum class enum_u64 : uint64_t { val = 1 };
    enum class enum_i8 : int8_t { val = 1 };
    enum class enum_i16 : int16_t { val = 1 };
    enum class enum_i32 : int32_t { val = 1 };
    enum class enum_i64 : int64_t { val = 1 };

    // The Entry<> struct defined by the JTrace subsystem is supposed to make it
    // easy to log either 32 or 64 bit arguments without needing to do a bunch
    // of explicit casting.  This compile time test makes sure that this is true
    // by exercising a number of different cases for both the large and small
    // entry types.
    Entry<UseLargeEntries::No> small;

    // Default
    small = SMALL_ENTRY("Test tag");

    // Numbers
    small = SMALL_ENTRY("Test tag", static_cast<unsigned char>(1));
    small = SMALL_ENTRY("Test tag", static_cast<uint8_t>(1));
    small = SMALL_ENTRY("Test tag", static_cast<uint16_t>(1));
    small = SMALL_ENTRY("Test tag", static_cast<uint32_t>(1));
    small = SMALL_ENTRY("Test tag", static_cast<char>(1));
    small = SMALL_ENTRY("Test tag", static_cast<int8_t>(1));
    small = SMALL_ENTRY("Test tag", static_cast<int16_t>(1));
    small = SMALL_ENTRY("Test tag", static_cast<int32_t>(1));

    // class enums
    small = SMALL_ENTRY("Test tag", enum_default::val);
    small = SMALL_ENTRY("Test tag", enum_u8::val);
    small = SMALL_ENTRY("Test tag", enum_u16::val);
    small = SMALL_ENTRY("Test tag", enum_u32::val);
    small = SMALL_ENTRY("Test tag", enum_i8::val);
    small = SMALL_ENTRY("Test tag", enum_i16::val);
    small = SMALL_ENTRY("Test tag", enum_i32::val);

    Entry<UseLargeEntries::Yes> large;

    // Default
    large = LARGE_ENTRY("Test tag");

    // Numbers (32-bit fields)
    large = LARGE_ENTRY("Test tag", static_cast<unsigned char>(1));
    large = LARGE_ENTRY("Test tag", static_cast<uint8_t>(1));
    large = LARGE_ENTRY("Test tag", static_cast<uint16_t>(1));
    large = LARGE_ENTRY("Test tag", static_cast<uint32_t>(1));
    large = LARGE_ENTRY("Test tag", static_cast<char>(1));
    large = LARGE_ENTRY("Test tag", static_cast<int8_t>(1));
    large = LARGE_ENTRY("Test tag", static_cast<int16_t>(1));
    large = LARGE_ENTRY("Test tag", static_cast<int32_t>(1));

    // class enums (32-bit fields)
    large = LARGE_ENTRY("Test tag", enum_default::val);
    large = LARGE_ENTRY("Test tag", enum_u8::val);
    large = LARGE_ENTRY("Test tag", enum_u16::val);
    large = LARGE_ENTRY("Test tag", enum_u32::val);
    large = LARGE_ENTRY("Test tag", enum_i8::val);
    large = LARGE_ENTRY("Test tag", enum_i16::val);
    large = LARGE_ENTRY("Test tag", enum_i32::val);

    // Numbers (64-bit fields)
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, static_cast<unsigned char>(1));
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, static_cast<uint8_t>(1));
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, static_cast<uint16_t>(1));
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, static_cast<uint32_t>(1));
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, static_cast<uint64_t>(1));
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, static_cast<char>(1));
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, static_cast<int8_t>(1));
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, static_cast<int16_t>(1));
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, static_cast<int32_t>(1));
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, static_cast<int64_t>(1));

    // class enums (64-bit fields)
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, enum_default::val);
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, enum_u8::val);
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, enum_u16::val);
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, enum_u32::val);
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, enum_u64::val);
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, enum_i8::val);
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, enum_i16::val);
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, enum_i32::val);
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, enum_i64::val);

    // Pointers
    struct Foo {
      uint32_t val;
    };
    struct Bar : fbl::RefCounted<Bar> {
      uint32_t val;
    };

    fbl::AllocChecker ac;
    auto foo = ktl::make_unique<Foo>(&ac);
    ASSERT_TRUE(ac.check());
    auto const_foo = ktl::make_unique<const Foo>(&ac);
    ASSERT_TRUE(ac.check());
    auto bar = fbl::MakeRefCountedChecked<Bar>(&ac);
    ASSERT_TRUE(ac.check());
    auto const_bar = fbl::MakeRefCountedChecked<const Bar>(&ac);
    ASSERT_TRUE(ac.check());

    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, reinterpret_cast<uint32_t*>(1));
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, reinterpret_cast<const uint32_t*>(1));
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, reinterpret_cast<Foo*>(1));
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, reinterpret_cast<const Foo*>(1));
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, foo);
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, const_foo);
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, bar);
    large = LARGE_ENTRY("Test tag", 0, 0, 0, 0, const_bar);

    // Make sure that the file/function/line info recorded in a LARGE_ENTRY
    // makes sense.
    constexpr int kExpectedLineNumber = __LINE__ + 1;
    large = LARGE_ENTRY("Test tag");
    EXPECT_EQ(__FILE__, large.ffl_info->file);
    EXPECT_EQ(EXPECTED_MAKE_ENTRY_FUNCTION, large.ffl_info->func);
    EXPECT_EQ(kExpectedLineNumber, large.ffl_info->line);

    END_TEST;
  }

  template <typename Config>
  static bool basic() {
    BEGIN_TEST;

    using Header = typename TestState<Config>::Header;
    using Entry = typename TestState<Config>::Entry;

    // Create our trace instance.
    fbl::AllocChecker ac;
    auto state = ktl::make_unique<TestState<Config>>(&ac);
    ASSERT_TRUE(ac.check());

    // Make sure that the trace buffer is large enough to run the tests below.
    static_assert(sizeof(state->trace_storage) > ((2 * sizeof(Header)) + sizeof(Entry)));

    // We have not set the location of our trace storage yet, so the internal
    // header and entry count fields should still be nullptr/0.
    ASSERT_NULL(state->trace.hdr());
    ASSERT_EQ(0u, state->trace.entry_cnt_);

    // Attempts to set a null storage location should be silently rejected.
    // Note that we have to make an effort to actually construct a span with a
    // null pointer and a non-null length.
    {
      uint8_t* null = nullptr;
      state->trace.SetLocation({null, sizeof(state->trace_storage)});
      ASSERT_NULL(state->trace.hdr());
      ASSERT_EQ(0u, state->trace.entry_cnt_);
    }

    // Attempts to set storage which is too small to hold a header should be
    // silently rejected.
    state->trace.SetLocation({state->trace_storage, 1});
    ASSERT_NULL(state->trace.hdr());
    ASSERT_EQ(0u, state->trace.entry_cnt_);

    // Attempts to set storage which can hold a header, but is too small to hold
    // a single entry should be silently rejected.
    state->trace.SetLocation({state->trace_storage, sizeof(Header) + 1});
    ASSERT_NULL(state->trace.hdr());
    ASSERT_EQ(0u, state->trace.entry_cnt_);

    // Attempts to set storage which not aligned to a trace header should be
    // silently rejected.
    state->trace.SetLocation({state->trace_storage + 1, sizeof(state->trace_storage) - 1});
    ASSERT_NULL(state->trace.hdr());
    ASSERT_EQ(0u, state->trace.entry_cnt_);

    // We do not have any trace storage configured yet, but attempts to log a
    // new trace entry should still not crash the system.
    JTRACE(state->trace, "Test tag", 1);
    state->hooks.Reset();
    state->trace.Dump(ZX_TIME_INFINITE);
    ASSERT_TRUE(state->hooks.CheckDumpFailed());

    // Now actually set the location of the trace storage, and check that the set stuck.
    state->trace.SetLocation({state->trace_storage, sizeof(state->trace_storage)});
    ASSERT_EQ(reinterpret_cast<Header*>(state->trace_storage), state->trace.hdr());
    ASSERT_EQ(TestState<Config>::kExpectedEntries, state->trace.entry_cnt_);

    // Try to reset the location of our trace storage again, with another legal
    // location.  This should be silently ignored.
    state->trace.SetLocation(
        {state->trace_storage + sizeof(Header), sizeof(state->trace_storage) - sizeof(Header)});
    ASSERT_EQ(reinterpret_cast<Header*>(state->trace_storage), state->trace.hdr());
    ASSERT_EQ(TestState<Config>::kExpectedEntries, state->trace.entry_cnt_);

    // Now that storage is configured, log some trace entries, and verify that
    // they show up when we dump the trace.
    static constexpr uint32_t kExpectedEntryCount = 10;
    ASSERT_LE(kExpectedEntryCount, state->trace.entry_cnt_);
    for (uint32_t i = 0; i < kExpectedEntryCount; ++i) {
      JTRACE(state->trace, "Test tag", i + 1);
    };
    state->hooks.Reset(1);
    state->trace.Dump(ZX_TIME_INFINITE);
    ASSERT_TRUE(state->hooks.CheckDumpSucceeded(kExpectedEntryCount));

    END_TEST;
  }

  template <typename Config>
  static bool wrapping() {
    BEGIN_TEST;

    // Create our trace instance and configure its storage.
    fbl::AllocChecker ac;
    auto state = ktl::make_unique<TestState<Config>>(&ac);
    ASSERT_TRUE(ac.check());
    state->trace.SetLocation({state->trace_storage, sizeof(state->trace_storage)});

    // Fill the storage up with log entries, but do not cause it to wrap just yet.
    ASSERT_EQ(TestState<Config>::kExpectedEntries, state->trace.entry_cnt_);
    uint32_t id = 0;
    for (uint32_t i = 0; i < state->trace.entry_cnt_; ++i) {
      JTRACE(state->trace, "Test tag", ++id);
    };

    // Verify that the entries we wrote are in the trace when we dump it.  The
    // entry IDs in the trace should currently be on the range
    // [1, entry_cnt_]
    state->hooks.Reset(1);
    state->trace.Dump(ZX_TIME_INFINITE);
    ASSERT_TRUE(state->hooks.CheckDumpSucceeded(TestState<Config>::kExpectedEntries));

    // Now wrap the trace, overwriting all but one of the original entries.
    ASSERT_GT(state->trace.entry_cnt_, 1u);
    for (uint32_t i = 0; i < state->trace.entry_cnt_ - 1; ++i) {
      JTRACE(state->trace, "Test tag", ++id);
    };

    // Verify that the entries we wrote are in the trace when we dump it.  The
    // entry IDs in the trace should currently be on the range
    // [entry_cnt_, (2 * entry_cnt_) - 1]
    state->hooks.Reset(state->trace.entry_cnt_);
    state->trace.Dump(ZX_TIME_INFINITE);
    ASSERT_TRUE(state->hooks.CheckDumpSucceeded(TestState<Config>::kExpectedEntries));

    END_TEST;
  }

  template <typename Config>
  static bool recovery() {
    BEGIN_TEST;

    // Create our trace instance and configure its storage.
    fbl::AllocChecker ac;
    auto state = ktl::make_unique<TestState<Config>>(&ac);
    ASSERT_TRUE(ac.check());
    state->trace.SetLocation({state->trace_storage, sizeof(state->trace_storage)});
    ASSERT_EQ(TestState<Config>::kExpectedEntries, state->trace.entry_cnt_);

    // Attempt to dump the recovered log.  For non-persistent configurations,
    // this should simply print a warning and exit.  For persistent configs,
    // this should print a single informational message indicating that the log
    // was "clean" (the magic number was zero) and therefore there was nothing
    // to dump.
    state->hooks.Reset();
    state->trace.DumpRecovered();
    if constexpr (Config::kIsPersistent == IsPersistent::No) {
      ASSERT_TRUE(state->hooks.CheckDumpFailed());
      return true;
    } else {
      ASSERT_TRUE(state->hooks.CheckNothingRecovered());
    }

    // Add some entries to the trace, then make a copy of the storage.  We will
    // use this image of a valid trace state as our template for the next parts
    // of the recovery test.
    constexpr uint32_t kGeneratedEntryCount = 1000;
    for (uint32_t i = 0; i < kGeneratedEntryCount; ++i) {
      JTRACE(state->trace, "Test tag", i + 1);
    };
    static_assert(sizeof(state->trace_storage) == sizeof(state->recovery_template));
    ::memcpy(state->recovery_template, state->trace_storage, sizeof(state->trace_storage));

    // The "recovered" trace should still contain nothing.
    state->hooks.Reset();
    state->trace.DumpRecovered();
    ASSERT_TRUE(state->hooks.CheckNothingRecovered());

    // Reset our trace instance, then assign the our storage back to the new
    // instance.  We should successfully recover the trace from the "previous
    // boot" and be able to dump the entries.
    constexpr uint32_t kExpectedEntries =
        ktl::min<uint32_t>(TestState<Config>::kExpectedEntries, kGeneratedEntryCount);
    constexpr uint32_t kExpectedFirstId =
        1 + (kGeneratedEntryCount > TestState<Config>::kExpectedEntries
                 ? kGeneratedEntryCount - TestState<Config>::kExpectedEntries
                 : 0);

    state->ResetTrace();
    state->trace.SetLocation({state->trace_storage, sizeof(state->trace_storage)});
    state->hooks.Reset(kExpectedFirstId);
    state->trace.DumpRecovered();
    ASSERT_TRUE(state->hooks.CheckDumpSucceeded(kExpectedEntries));

    // Reset our trace instance, and corrupt the magic number before assigning
    // our storage and recovering the trace.  We should receive a warning that
    // the header was corrupted along with a hexdump of the trace buffer when we
    // attempt to dump the recovered trace.
    using Header = typename TestState<Config>::Header;
    state->ResetTrace();
    reinterpret_cast<Header*>(state->trace_storage)->magic = 0x12345678;
    state->trace.SetLocation({state->trace_storage, sizeof(state->trace_storage)});
    state->hooks.Reset();
    state->trace.DumpRecovered();
    ASSERT_TRUE(state->hooks.CheckDumpCorrupt());

    // Reset our trace instance, restore our template, then corrupt the write
    // pointer.  Verify that the recovered trace is reported as corrupt when we
    // attempt to dump it.
    state->ResetTrace();
    ::memcpy(state->trace_storage, state->recovery_template, sizeof(state->trace_storage));
    reinterpret_cast<Header*>(state->trace_storage)->wr = TestState<Config>::kExpectedEntries + 10;
    state->trace.SetLocation({state->trace_storage, sizeof(state->trace_storage)});
    state->hooks.Reset();
    state->trace.DumpRecovered();
    ASSERT_TRUE(state->hooks.CheckDumpCorrupt());

    END_TEST;
  }

  template <typename BaseConfig>
  static bool per_cpu_last_entries() {
    BEGIN_TEST;

    // Define a version of our config with per-cpu last entries enabled.  Set
    // the storage to be equal to the maximum number of SMP CPUs currently
    // supported.
    using Config = ::jtrace::Config<4096, SMP_MAX_CPUS, BaseConfig::kIsPersistent,
                                    BaseConfig::kUseLargeEntries>;

    // Create our trace instance and configure its storage.
    fbl::AllocChecker ac;
    auto state = ktl::make_unique<TestState<Config>>(&ac);
    ASSERT_TRUE(ac.check());
    state->trace.SetLocation({state->trace_storage, sizeof(state->trace_storage)});

    // Turn off preemption so that we cannot migrate to a new CPU, then create a few trace entries.
    // Make sure that we take note of the CPU we were running on when we made the entries.
    uint32_t id = 0;
    cpu_num_t expected_cpu = -1;
    {
      AutoPreemptDisabler preempt_disable;
      expected_cpu = arch_curr_cpu_num();
      JTRACE(state->trace, "Test tag", ++id);
      JTRACE(state->trace, "Test tag", ++id);
      JTRACE(state->trace, "Test tag", ++id);
    }

    // Set up our hooks to know which CPU should have created the per_cpu last entry;
    state->hooks.Reset(1, expected_cpu, id);
    state->trace.Dump(ZX_TIME_INFINITE);
    ASSERT_TRUE(state->hooks.CheckDumpSucceeded(3u));

    END_TEST;
  }
};
}  // namespace jtrace

UNITTEST_START_TESTCASE(jtrace_tests)
UNITTEST("entries", jtrace::tests::entries)
UNITTEST("basic (large entries, no persist)", jtrace::tests::basic<CfgLargeEntries>)
UNITTEST("basic (small entries, no persist)", jtrace::tests::basic<CfgSmallEntries>)
UNITTEST("basic (large entries, persist)", jtrace::tests::basic<CfgPersistLargeEntries>)
UNITTEST("basic (small entries, persist)", jtrace::tests::basic<CfgPersistSmallEntries>)
UNITTEST("wrapping (large entries, no persist)", jtrace::tests::wrapping<CfgLargeEntries>)
UNITTEST("wrapping (small entries, no persist)", jtrace::tests::wrapping<CfgSmallEntries>)
UNITTEST("wrapping (large entries, persist)", jtrace::tests::wrapping<CfgPersistLargeEntries>)
UNITTEST("wrapping (small entries, persist)", jtrace::tests::wrapping<CfgPersistSmallEntries>)
UNITTEST("recovery (large entries, no persist)", jtrace::tests::recovery<CfgLargeEntries>)
UNITTEST("recovery (small entries, no persist)", jtrace::tests::recovery<CfgSmallEntries>)
UNITTEST("recovery (large entries, persist)", jtrace::tests::recovery<CfgPersistLargeEntries>)
UNITTEST("recovery (small entries, persist)", jtrace::tests::recovery<CfgPersistSmallEntries>)
UNITTEST("per_cpu_last_entries (large entries, no persist)",
         jtrace::tests::per_cpu_last_entries<CfgLargeEntries>)
UNITTEST("per_cpu_last_entries (small entries, no persist)",
         jtrace::tests::per_cpu_last_entries<CfgSmallEntries>)
UNITTEST("per_cpu_last_entries (large entries, persist)",
         jtrace::tests::per_cpu_last_entries<CfgPersistLargeEntries>)
UNITTEST("per_cpu_last_entries (small entries, persist)",
         jtrace::tests::per_cpu_last_entries<CfgPersistSmallEntries>)
UNITTEST_END_TESTCASE(jtrace_tests, "jtrace", "Debug trace tests")
