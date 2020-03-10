// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <zircon/syscalls.h>

#include <array>
#include <memory>

#include <fbl/algorithm.h>
#include <fbl/string_printf.h>
#include <fbl/vector.h>
#include <trace-engine/instrumentation.h>
#include <trace-test-utils/fixture.h>
#include <trace/event.h>

#include "fixture_macros.h"

namespace {

bool blob_test() {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  const char name[] = "name";
  trace_string_ref_t name_ref = trace_make_inline_c_string_ref(name);
  const char blob[] = "abc";
  const size_t length = sizeof(blob);
  const char preview[] = "<61 62 63 00>";

  {
    auto context = trace::TraceContext::Acquire();

    trace_context_write_blob_record(context.get(), TRACE_BLOB_TYPE_DATA, &name_ref, blob, length);
  }

  auto expected =
      fbl::StringPrintf("Blob(name: %s, size: %zu, preview: %s)\n", name, length, preview);
  EXPECT_TRUE(fixture_compare_records(expected.c_str()), "record mismatch");

  END_TRACE_TEST;
}

bool blob_macro_test() {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  const char name[] = "all-byte-values";
  const size_t length = 256;
  auto blob = std::make_unique<std::array<char, length>>();
  for (unsigned i = 0; i < length; ++i) {
    blob->at(i) = static_cast<char>(i);
  }
  const char preview[] = "<00 01 02 03 04 05 06 07 ... f8 f9 fa fb fc fd fe ff>";

  TRACE_BLOB(TRACE_BLOB_TYPE_DATA, name, blob->data(), length);
  auto expected = fbl::StringPrintf(
      "String(index: 1, \"%s\")\n"
      "Blob(name: %s, size: %zu, preview: %s)\n",
      name, name, length, preview);
  EXPECT_TRUE(fixture_compare_records(expected.c_str()), "record mismatch");

  END_TRACE_TEST;
}

bool large_blob_attachment_test() {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  const char category[] = "+enabled";
  const char name[] = "name";
  trace_string_ref_t category_ref = trace_make_inline_c_string_ref(category);
  trace_string_ref_t name_ref = trace_make_inline_c_string_ref(name);

  const size_t length = (1ull << 15);  // 32KB
  auto blob = std::make_unique<std::array<char, length>>();
  for (unsigned i = 0; i < length; ++i) {
    blob->at(i) = static_cast<char>(i);
  }
  const char preview[] = "<00 01 02 03 04 05 06 07 ... f8 f9 fa fb fc fd fe ff>";

  {
    auto context = trace::TraceContext::Acquire();

    trace_context_write_blob_attachment_record(context.get(), &category_ref, &name_ref,
                                               blob->data(), length);
  }

  auto expected = fbl::StringPrintf(
      "LargeRecord(Blob(format: blob_attachment, category: \"%s\", name: \"%s\", size: %zu, "
      "preview: %s))\n",
      category, name, length, preview);
  EXPECT_TRUE(fixture_compare_records(expected.c_str()), "record mismatch");

  END_TRACE_TEST;
}

bool large_blob_attachment_macro_test() {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  const char name[] = "all-byte-values";
  const size_t length = 260;
  auto blob = std::make_unique<std::array<char, length>>();
  for (unsigned i = 0; i < length; ++i) {
    blob->at(i) = static_cast<char>(i);
  }
  const char preview[] = "<00 01 02 03 04 05 06 07 ... fc fd fe ff 00 01 02 03>";

  const char* category = "+enabled";
  TRACE_BLOB_ATTACHMENT(category, name, blob->data(), length);
  auto expected = fbl::StringPrintf(
      "String(index: 1, \"%s\")\n"
      "String(index: 2, \"%s\")\n"
      "LargeRecord(Blob(format: blob_attachment, category: \"%s\", name: \"%s\", size: %zu, "
      "preview: %s))\n",
      category, name, category, name, length, preview);
  EXPECT_TRUE(fixture_compare_records(expected.c_str()), "record mismatch");

  END_TRACE_TEST;
}

bool large_blob_event_macro_args_test() {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  const char name[] = "all-byte-values";
  const size_t length = 256;
  auto blob = std::make_unique<std::array<char, length>>();
  for (unsigned i = 0; i < length; ++i) {
    blob->at(i) = static_cast<char>(i);
  }
  const char preview[] = "<00 01 02 03 04 05 06 07 ... f8 f9 fa fb fc fd fe ff>";

  const char* category = "+enabled";
  TRACE_BLOB_EVENT(category, name, blob->data(), length, "arg1", TA_INT32(234234));
  auto expected = fbl::StringPrintf(
      "String(index: 1, \"%s\")\n"
      "String(index: 2, \"process\")\n"
      "KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n"
      "Thread(index: 1, <>)\n"
      "String(index: 3, \"%s\")\n"
      "String(index: 4, \"arg1\")\n"
      "LargeRecord(Blob(format: blob_event, category: \"%s\", name: \"%s\", ts: <>, pt: <>, {arg1: "
      "int32(234234)}, "
      "size: %zu, preview: %s))\n",
      category, name, category, name, length, preview);
  EXPECT_TRUE(fixture_compare_records(expected.c_str()), "record mismatch");

  END_TRACE_TEST;
}

bool large_blob_event_macro_small_test() {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  const char name[] = "all-byte-values";
  const size_t length = 260;
  auto blob = std::make_unique<std::array<char, length>>();
  for (unsigned i = 0; i < length; ++i) {
    blob->at(i) = static_cast<char>(i);
  }
  const char preview[] = "<00 01 02 03 04 05 06 07 ... fc fd fe ff 00 01 02 03>";

  const char* category = "+enabled";
  TRACE_BLOB_EVENT(category, name, blob->data(), length);
  auto expected = fbl::StringPrintf(
      "String(index: 1, \"%s\")\n"
      "String(index: 2, \"process\")\n"
      "KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n"
      "Thread(index: 1, <>)\n"
      "String(index: 3, \"%s\")\n"
      "LargeRecord(Blob(format: blob_event, category: \"%s\", name: \"%s\", ts: <>, pt: <>, {}, "
      "size: %zu, preview: %s))\n",
      category, name, category, name, length, preview);
  EXPECT_TRUE(fixture_compare_records(expected.c_str()), "record mismatch");

  END_TRACE_TEST;
}

bool large_blob_event_macro_medium_test() {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  const char name[] = "all-byte-values";
  const size_t length = (1ull << 15);  // 32KB
  auto blob = std::make_unique<std::array<char, length>>();
  for (unsigned i = 0; i < length; ++i) {
    blob->at(i) = static_cast<char>(i);
  }
  const char preview[] = "<00 01 02 03 04 05 06 07 ... f8 f9 fa fb fc fd fe ff>";

  const char* category = "+enabled";
  TRACE_BLOB_EVENT(category, name, blob->data(), length);
  auto expected = fbl::StringPrintf(
      "String(index: 1, \"%s\")\n"
      "String(index: 2, \"process\")\n"
      "KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n"
      "Thread(index: 1, <>)\n"
      "String(index: 3, \"%s\")\n"
      "LargeRecord(Blob(format: blob_event, category: \"%s\", name: \"%s\", ts: <>, pt: <>, {}, "
      "size: %zu, preview: %s))\n",
      category, name, category, name, length, preview);
  EXPECT_TRUE(fixture_compare_records(expected.c_str()), "record mismatch");

  END_TRACE_TEST;
}

bool large_blob_event_macro_big_test() {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  const char name[] = "all-byte-values";
  const size_t length = TRACE_ENCODED_INLINE_LARGE_RECORD_MAX_SIZE - 356;
  auto blob = std::make_unique<std::array<char, length>>();
  for (unsigned i = 0; i < length; ++i) {
    blob->at(i) = static_cast<char>(i);
  }
  const char preview[] = "<00 01 02 03 04 05 06 07 ... 94 95 96 97 98 99 9a 9b>";

  const char* category = "+enabled";
  TRACE_BLOB_EVENT(category, name, blob->data(), length);
  auto expected = fbl::StringPrintf(
      "String(index: 1, \"%s\")\n"
      "String(index: 2, \"process\")\n"
      "KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n"
      "Thread(index: 1, <>)\n"
      "String(index: 3, \"%s\")\n"
      "LargeRecord(Blob(format: blob_event, category: \"%s\", name: \"%s\", ts: <>, pt: <>, {}, "
      "size: %zu, preview: %s))\n",
      category, name, category, name, length, preview);
  EXPECT_TRUE(fixture_compare_records(expected.c_str()), "record mismatch");

  END_TRACE_TEST;
}

bool large_blob_event_macro_rejected_test() {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  const char name[] = "all-byte-values";

  const size_t length = TRACE_ENCODED_INLINE_LARGE_RECORD_MAX_SIZE + 100;
  auto blob = std::make_unique<std::array<char, length>>();
  for (unsigned i = 0; i < length; ++i) {
    blob->at(i) = static_cast<char>(i);
  }

  const char* category = "+enabled";
  TRACE_BLOB_EVENT(category, name, blob->data(), length);
  auto expected = fbl::StringPrintf(
      "String(index: 1, \"%s\")\n"
      "String(index: 2, \"process\")\n"
      "KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n"
      "Thread(index: 1, <>)\n"
      "String(index: 3, \"%s\")\n",
      category, name);
  EXPECT_TRUE(fixture_compare_records(expected.c_str()), "record mismatch");

  END_TRACE_TEST;
}

bool arg_value_null_ending_test() {
  BEGIN_TRACE_TEST;

  fixture_initialize_and_start_tracing();

  static const char terminated[5] = {'1', '2', '3', '4', '\0'};
  static const char unterminated[5] = {'1', '2', '3', '4', '5'};

  TRACE_DURATION_BEGIN("+enabled", "name", "key", "literal");
  TRACE_DURATION_BEGIN("+enabled", "name", "key", terminated);
  TRACE_DURATION_BEGIN("+enabled", "name", "key", unterminated);
  fbl::Vector<trace::Record> records;

  fixture_stop_and_terminate_tracing();

  EXPECT_TRUE(fixture_read_records(&records));

  EXPECT_EQ(records.size(), 10);
  EXPECT_TRUE(fixture_compare_raw_records(
      records, 1, 6,
      "String(index: 1, \"+enabled\")\n"
      "String(index: 2, \"process\")\n"
      "KernelObject(koid: <>, type: thread, name: \"initial-thread\", {process: koid(<>)})\n"
      "Thread(index: 1, <>)\n"
      "String(index: 3, \"name\")\n"
      "String(index: 4, \"key\")\n"));

  // The comparison in the fixture_compare_*_records functions does not distinguish between strings
  // that match up to the first null character. These checks ensure that the terminal null character
  // is not included in the string argument values.
  const auto& literal_arg = records[7].GetEvent().arguments[0].value().GetString();
  EXPECT_EQ(literal_arg.length(), 7);
  EXPECT_STR_EQ(literal_arg.c_str(), "literal");
  const auto& terminated_arg = records[8].GetEvent().arguments[0].value().GetString();
  EXPECT_EQ(terminated_arg.length(), 4);
  EXPECT_STR_EQ(terminated_arg.c_str(), "1234");
  const auto& unterminated_arg = records[9].GetEvent().arguments[0].value().GetString();
  EXPECT_EQ(unterminated_arg.length(), 5);
  EXPECT_STR_EQ(unterminated_arg.c_str(), "12345");

  END_TRACE_TEST;
}

}  // namespace

BEGIN_TEST_CASE(records)
RUN_TEST(blob_test)
RUN_TEST(blob_macro_test)
RUN_TEST(large_blob_attachment_test)
RUN_TEST(large_blob_attachment_macro_test)
RUN_TEST(large_blob_event_macro_args_test)
RUN_TEST(large_blob_event_macro_small_test)
RUN_TEST(large_blob_event_macro_medium_test)
RUN_TEST(large_blob_event_macro_big_test)
RUN_TEST(large_blob_event_macro_rejected_test)
RUN_TEST(arg_value_null_ending_test)
END_TEST_CASE(records)
