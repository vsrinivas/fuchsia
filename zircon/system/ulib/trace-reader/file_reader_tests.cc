// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-reader/file_reader.h>

#include <memory>
#include <stdint.h>
#include <stdio.h>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/vector.h>
#include <trace-engine/fields.h>
#include <trace-engine/types.h>
#include <zxtest/zxtest.h>

#include "reader_tests.h"

namespace trace {
namespace {

const char kTestInputFile[] = "/tmp/trace-reader-test.fxt";

TEST(TraceFileReader, Records) {
  FILE* f = fopen(kTestInputFile, "wb");
  ASSERT_NOT_NULL(f);

  constexpr zx_koid_t kProcessKoid = 42;
  constexpr zx_koid_t kThreadKoid = 43;
  constexpr trace_thread_index_t kThreadIndex = 99;

  uint64_t thread_record[3]{};
  ThreadRecordFields::Type::Set(thread_record[0], static_cast<uint64_t>(RecordType::kThread));
  ThreadRecordFields::RecordSize::Set(thread_record[0], fbl::count_of(thread_record));
  ThreadRecordFields::ThreadIndex::Set(thread_record[0], kThreadIndex);
  thread_record[1] = kProcessKoid;
  thread_record[2] = kThreadKoid;

  ASSERT_EQ(fwrite(&thread_record[0], sizeof(thread_record[0]), fbl::count_of(thread_record), f),
            fbl::count_of(thread_record));
  ASSERT_EQ(fclose(f), 0);

  std::unique_ptr<trace::FileReader> reader;
  fbl::Vector<trace::Record> records;
  fbl::String error;
  ASSERT_TRUE(trace::FileReader::Create(kTestInputFile, test::MakeRecordConsumer(&records),
                                        test::MakeErrorHandler(&error), &reader));

  reader->ReadFile();
  EXPECT_TRUE(error.empty());
  ASSERT_EQ(records.size(), 1u);
  const trace::Record& rec = records[0];
  EXPECT_EQ(rec.type(), RecordType::kThread);
  const trace::Record::Thread& thread = rec.GetThread();
  EXPECT_EQ(thread.index, kThreadIndex);
  EXPECT_EQ(thread.process_thread.process_koid(), kProcessKoid);
  EXPECT_EQ(thread.process_thread.thread_koid(), kThreadKoid);
}

// NOTE: Most of the reader is covered by the libtrace tests.

}  // namespace
}  // namespace trace
