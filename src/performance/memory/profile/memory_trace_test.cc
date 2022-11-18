// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/span.h>

#include <optional>

#include <gtest/gtest.h>
#include <trace-test-utils/fixture.h>

#include "src/performance/memory/profile/memory_layout.h"
#include "src/performance/memory/profile/stack_compression.h"
#include "src/performance/memory/profile/test_record_container.h"
#include "src/performance/memory/profile/trace_constants.h"

extern const char* trace_category;

namespace {

const char* trace_category_enabled = "+memory_trace";
const char* trace_category_disabled = "-memory_trace";

struct FixtureCleanup {
  ~FixtureCleanup() { fixture_tear_down(); }
};

// Returns the record with the given name and the given ptr if specified, and nullopt otherwise.
std::optional<std::reference_wrapper<const trace::LargeRecordData::BlobEvent>> find_blob_record(
    const fbl::Vector<trace::Record>& records, const char* name, std::optional<void*> ptr) {
  for (auto& r : records) {
    if (r.type() != trace::RecordType::kLargeRecord)
      continue;
    auto& l = r.GetLargeRecord();
    if (l.type() != trace::LargeRecordType::kBlob)
      continue;
    auto& b = std::get<trace::LargeRecordData::BlobEvent>(l.GetBlob());

    if (b.category != trace_category_enabled)
      continue;
    if (b.name != name)
      continue;
    if (ptr != std::nullopt) {
      if (b.arguments.size() <= 0)
        continue;
      EXPECT_EQ(b.arguments[0].name(), ADDR);
      if (b.arguments[0].value().GetUint64() != reinterpret_cast<uint64_t>(*ptr))
        continue;
    }
    return {b};
  }
  return std::nullopt;
}

std::optional<std::reference_wrapper<const trace::Record::Event>> find_instant_record(
    const fbl::Vector<trace::Record>& records, const char* name, std::optional<void*> ptr) {
  for (auto& r : records) {
    if (r.type() != trace::RecordType::kEvent)
      continue;
    auto& b = r.GetEvent();
    if (b.type() != trace::EventType::kInstant)
      continue;
    if (b.category != trace_category_enabled)
      continue;
    if (b.name != name)
      continue;
    if (ptr != std::nullopt) {
      if (b.arguments.size() <= 0)
        continue;
      EXPECT_EQ(b.arguments[0].name(), ADDR);
      if (b.arguments[0].value().GetUint64() != reinterpret_cast<uint64_t>(*ptr))
        continue;
    }
    return {b};
  }
  return std::nullopt;
}

// Returns a vector holding backtrace pointers.
std::vector<uint64_t> backtrace(
    std::optional<std::reference_wrapper<const trace::LargeRecordData::BlobEvent>> event) {
  if (event == std::nullopt)
    return {};

  uint64_t buffer[256];
  auto bt = decompress(
      {reinterpret_cast<const uint8_t*>(event->get().blob), event->get().blob_size}, buffer);
  return {bt.begin(), bt.end()};
}

std::optional<uint64_t> find_module_index(const Layout& layout, uint64_t code_ptr) {
  for (const auto& map : layout.mmaps) {
    if (map.starting_address <= code_ptr && code_ptr < map.starting_address + map.size) {
      return {map.module_index};
    }
  }
  return std::nullopt;
}

void verify_backtrace(const Layout& layout, const std::vector<uint64_t>& trace) {
  for (auto ptr : trace) {
    EXPECT_NE(find_module_index(layout, ptr), std::nullopt);
  }
}

// TODO(fxb/114682): Enable this test section.
TEST(MemoryTraceTest, DISABLED_Alloc) {
  FixtureCleanup cleanup;
  const size_t kBufferSize = 65536;
  fixture_set_up(kNoAttachToThread,  // No loop to attach.
                 TRACE_BUFFERING_MODE_ONESHOT, kBufferSize);
  fixture_initialize_and_start_tracing();

  trace_category = trace_category_enabled;  // Enable tracing for the fixture.

before_alloc_a:
  char* a = new char[859];
after_alloc_a:
before_alloc_b:
  char* b = new char[857];
after_alloc_b:
  EXPECT_NE(a, b);  // This also prevents allocations to be optimized out.
  delete[] a;
  delete[] b;

  trace_category = trace_category_disabled;

  fixture_stop_and_terminate_tracing();

  TestRecordContainer record_container;
  ASSERT_TRUE(record_container.ReadFromFixture());

  // Check the memory layout

  Layout layout;
  auto layout_record = find_blob_record(record_container.records(), LAYOUT, std::nullopt);
  ASSERT_NE(layout_record, std::nullopt) << record_container;
  std::istringstream is(std::string(reinterpret_cast<const char*>(layout_record->get().blob),
                                    layout_record->get().blob_size));

  layout.Read(is);
  EXPECT_EQ(is.get(), EOF);

  {
    // Check allocation events.
    auto alloc_a = find_blob_record(record_container.records(), ALLOC, a);
    ASSERT_NE(alloc_a, std::nullopt) << record_container;
    ASSERT_EQ(alloc_a->get().arguments[1].ToString(), std::string("size: uint64(859)"))
        << record_container;
    auto alloc_a_bt = backtrace(alloc_a);
    verify_backtrace(layout, alloc_a_bt);

    auto alloc_b = find_blob_record(record_container.records(), ALLOC, b);
    ASSERT_NE(alloc_b, std::nullopt) << record_container;
    ASSERT_EQ(alloc_b->get().arguments[1].ToString(), std::string("size: uint64(857)"))
        << record_container;
    auto alloc_b_bt = backtrace(alloc_b);
    verify_backtrace(layout, alloc_b_bt);

    // Check that the backtraces have the same prefix, and that the last return address differs.
    ASSERT_EQ(alloc_a_bt.size(), alloc_b_bt.size());
    int difference_count = 0;
    for (size_t i = 0; i < alloc_b_bt.size(); i++) {
      if (alloc_a_bt[i] != alloc_b_bt[i]) {
        difference_count++;
        // && here returns the code address of the label, which makes possible
        // to verify the tracktrace pointer points athe the function call.
        // This is not perfect as we don't have guarantee, but in practice
        // this is a good way to test that the backtrace was not corrupted.
        EXPECT_LE(reinterpret_cast<uint64_t>(&&before_alloc_a), alloc_a_bt[i]);
        EXPECT_LE(alloc_a_bt[i], reinterpret_cast<uint64_t>(&&after_alloc_a));

        EXPECT_LE(reinterpret_cast<uint64_t>(&&before_alloc_b), alloc_b_bt[i]);
        EXPECT_LE(alloc_b_bt[i], reinterpret_cast<uint64_t>(&&after_alloc_b));
      }
    }
    EXPECT_EQ(difference_count, 1);
  }

  {
    // Verify deallocation event presence.
    auto dealloc_a = find_instant_record(record_container.records(), DEALLOC, a);
    ASSERT_NE(dealloc_a, std::nullopt) << record_container;

    auto dealloc_b = find_instant_record(record_container.records(), DEALLOC, b);
    ASSERT_NE(dealloc_b, std::nullopt) << record_container;
  }
}

// TODO(fxb/114682): Enable this test.
TEST(MemoryTraceTest, DISABLED_LayoutIsSent) {
  FixtureCleanup cleanup;
  const size_t kBufferSize = 65536;
  fixture_set_up(kNoAttachToThread,  // No loop to attach.
                 TRACE_BUFFERING_MODE_ONESHOT, kBufferSize);
  fixture_initialize_and_start_tracing();

  // Verify that a layout record is emitted for each session.
  trace_category = trace_category_enabled;
  char* a = new char[859];
  trace_category = trace_category_disabled;
  delete[] a;

  trace_category = trace_category_enabled;
  char* b = new char[859];
  trace_category = trace_category_disabled;
  delete[] b;

  fixture_stop_and_terminate_tracing();

  TestRecordContainer record_container;
  ASSERT_TRUE(record_container.ReadFromFixture());

  int layout_count = 0;
  for (auto& r : record_container.records()) {
    if (r.type() != trace::RecordType::kLargeRecord)
      continue;
    auto& l = r.GetLargeRecord();
    if (l.type() != trace::LargeRecordType::kBlob)
      continue;
    auto& b = std::get<trace::LargeRecordData::BlobEvent>(l.GetBlob());
    if (b.category != trace_category_enabled)
      continue;
    if (b.name != LAYOUT)
      continue;
    layout_count++;
  }
  EXPECT_EQ(layout_count, 2);
}

}  // namespace
