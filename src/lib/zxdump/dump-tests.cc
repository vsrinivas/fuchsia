// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dump-tests.h"

#include <lib/zxdump/dump.h>
#include <lib/zxdump/fd-writer.h>
#include <lib/zxdump/task.h>

#include <gtest/gtest.h>

#include "test-file.h"

namespace {

using namespace zxdump::testing;

// The dump format is complex enough that direct testing of output data would
// be tantamount to reimplementing the reader, and golden binary files aren't
// easy to match up with fresh data from a live system where all the KOID and
// statistics values will be different every time.  So the main method used to
// test the dumper is via end-to-end tests that dump into a file via the dumper
// API, read the dump back using the reader API, and then compare the data from
// the dump to the data from the original live tasks.

TEST(ZxdumpTests, ProcessDumpBasic) {
  TestFile file;
  zxdump::FdWriter writer(file.RewoundFd());

  TestProcess process;
  ASSERT_NO_FATAL_FAILURE(process.StartChild());
  zxdump::ProcessDump<zx::unowned_process> dump(process.borrow());

  auto collect_result = dump.CollectProcess(TestProcess::PruneAllMemory);
  ASSERT_TRUE(collect_result.is_ok()) << collect_result.error_value();

  auto dump_result = dump.DumpHeaders(writer.AccumulateFragmentsCallback());
  ASSERT_TRUE(dump_result.is_ok()) << dump_result.error_value();

  auto write_result = writer.WriteFragments();
  ASSERT_TRUE(write_result.is_ok()) << write_result.error_value();
  const size_t bytes_written = write_result.value();

  auto memory_result = dump.DumpMemory(writer.WriteCallback());
  ASSERT_TRUE(memory_result.is_ok()) << memory_result.error_value();
  const size_t total_with_memory = memory_result.value();

  // We pruned all memory, so DumpMemory should not have added any output.
  EXPECT_EQ(bytes_written, total_with_memory);

  // Now read the file back in.
  zxdump::TaskHolder holder;
  auto read_result = holder.Insert(file.RewoundFd());
  ASSERT_TRUE(read_result.is_ok()) << read_result.error_value();

  // The dump has no jobs, so there should be a placeholder "super-root".
  EXPECT_EQ(ZX_KOID_INVALID, holder.root_job().koid());

  auto processes = holder.root_job().processes();
  ASSERT_TRUE(processes.is_ok()) << processes.error_value();

  // The fake job should have exactly one process.
  EXPECT_EQ(processes->get().size(), 1u);
  for (auto& [read_koid, read_process] : processes->get()) {
    EXPECT_NE(read_koid, ZX_KOID_INVALID);

    // Get the basic info from the real live process handle.
    zx_info_handle_basic_t basic;
    ASSERT_EQ(ZX_OK, process.borrow()->get_info(ZX_INFO_HANDLE_BASIC, &basic, sizeof(basic),
                                                nullptr, nullptr));
    EXPECT_EQ(read_koid, basic.koid);
    EXPECT_EQ(ZX_OBJ_TYPE_PROCESS, basic.type);

    // Get the same info from the dump and verify they match up.  Note that the
    // zx_info_handle_basic_t::rights in the dump is not usually particularly
    // meaningful about the dumped process, because it's just whatever rights
    // the dumper's own process handle had.  But in this case it does exactly
    // match the handle we just checked, since that's what we used to dump.
    auto read_basic = read_process.get_info<ZX_INFO_HANDLE_BASIC>();
    ASSERT_TRUE(read_basic.is_ok()) << read_basic.error_value();
    EXPECT_EQ(basic.koid, read_basic->koid);
    EXPECT_EQ(basic.rights, read_basic->rights);
    EXPECT_EQ(basic.type, read_basic->type);
    EXPECT_EQ(basic.related_koid, read_basic->related_koid);
  }
}

}  // namespace
