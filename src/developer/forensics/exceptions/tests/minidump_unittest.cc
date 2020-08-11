// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/handler/minidump.h"

#include <gtest/gtest.h>

#include "src/developer/forensics/exceptions/tests/crasher_wrapper.h"
#include "third_party/crashpad/snapshot/minidump/process_snapshot_minidump.h"

namespace forensics {
namespace exceptions {
namespace handler {
namespace {

constexpr char kData[] = "1234567489";

TEST(MinidumpTest, EmptyStringFileShouldFail) {
  crashpad::StringFile string_file;
  zx::vmo vmo;

  // Empty string file should not work.
  vmo = GenerateVMOFromStringFile(string_file);
  ASSERT_FALSE(vmo.is_valid());
}

TEST(MinidumpTest, GenerateVMOFromStringFile) {
  crashpad::StringFile string_file;
  zx::vmo vmo;

  ASSERT_TRUE(string_file.Write(kData, sizeof(kData)));
  vmo = GenerateVMOFromStringFile(string_file);
  ASSERT_TRUE(vmo.is_valid());

  // vmo sizes get rounded up to the next page size boundary, so we cannot expect exact size match.
  uint64_t size;
  ASSERT_EQ(vmo.get_size(&size), ZX_OK);
  EXPECT_LT(sizeof(kData), size);
}

TEST(MinidumpTest, GenerateMinidump) {
  ExceptionContext ec;
  if (!SpawnCrasher(&ec)) {
    FAIL() << "Could not initialize exception.";
    return;
  }
  ASSERT_TRUE(MarkExceptionAsHandled(&ec));

  zx::vmo minidump_vmo = GenerateMinidump(std::move(ec.exception));
  ASSERT_TRUE(minidump_vmo.is_valid());

  uint64_t vmo_size;
  ASSERT_EQ(minidump_vmo.get_size(&vmo_size), ZX_OK);

  auto buf = std::make_unique<uint8_t[]>(vmo_size);
  ASSERT_EQ(minidump_vmo.read(buf.get(), 0, vmo_size), ZX_OK);

  // Read the vmo back into a file writer/reader interface.
  crashpad::StringFile string_file;
  string_file.Write(buf.get(), vmo_size);

  // Move the cursor to the beggining of the file.
  ASSERT_EQ(string_file.Seek(0, SEEK_SET), 0);

  // We verify that the minidump snapshot can validly read the file.
  crashpad::ProcessSnapshotMinidump minidump_snapshot;
  ASSERT_TRUE(minidump_snapshot.Initialize(&string_file));

  // We kill the jobs. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  ec.job.kill();
}

}  // namespace
}  // namespace handler
}  // namespace exceptions
}  // namespace forensics
