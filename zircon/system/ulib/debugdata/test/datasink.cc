// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/debugdata/datasink.h>
#include <stdlib.h>

#include <fbl/unique_fd.h>
#include <src/lib/files/file.h>
#include <src/lib/files/scoped_temp_dir.h>
#include <zxtest/zxtest.h>

namespace {

constexpr char kTestSink[] = "test";
constexpr char kProfileSink[] = "llvm-profile";
constexpr uint8_t kTestData[] = {0x00, 0x11, 0x22, 0x33};
constexpr char kTestProfile[] = "test-profile";

TEST(DataSinkTest, ProcessData) {
  files::ScopedTempDir root_tmp_dir;
  std::string tmp_location;
  ASSERT_TRUE(root_tmp_dir.NewTempDir(&tmp_location));
  fbl::unique_fd tmp_dir(open(tmp_location.c_str(), O_RDWR | O_DIRECTORY));
  ASSERT_TRUE(tmp_dir.is_valid());
  debugdata::DataSink data_sink(tmp_dir);

  debugdata::DataSinkCallback on_data_collection_error_callback = [&](const std::string& error) {
    FAIL("Got error %s during data collection", error.c_str());
  };
  debugdata::DataSinkCallback on_data_collection_warning_callback =
      [&](const std::string& warning) {
        FAIL("Got warning %s during data collection", warning.c_str());
      };

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
  ASSERT_OK(vmo.write(kTestData, 0, sizeof(kTestData)));
  ASSERT_OK(vmo.set_prop_content_size(sizeof(kTestData)));

  data_sink.ProcessSingleDebugData(kTestSink, std::move(vmo), on_data_collection_error_callback,
                                   on_data_collection_warning_callback);

  zx::vmo profile_vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &profile_vmo));
  ASSERT_OK(profile_vmo.set_property(ZX_PROP_NAME, kTestProfile, sizeof(kTestProfile)));
  ASSERT_OK(profile_vmo.write(kTestData, 0, sizeof(kTestData)));
  data_sink.ProcessSingleDebugData(kProfileSink, std::move(profile_vmo),
                                   on_data_collection_error_callback,
                                   on_data_collection_warning_callback);

  debugdata::DataSinkFileMap written_files = data_sink.FlushToDirectory(
      on_data_collection_error_callback, on_data_collection_warning_callback);
  ASSERT_EQ(written_files.size(), 2);
  ASSERT_EQ(written_files[kProfileSink].size(), 1);
  debugdata::DumpFile expected_profile_file = {kTestProfile,
                                               std::string("llvm-profile/") + kTestProfile};
  ASSERT_EQ(*written_files[kProfileSink].begin(), expected_profile_file);
  ASSERT_EQ(written_files[kTestSink].size(), 1);
  char test_sink_location[256];
  sprintf(test_sink_location, "%s/%s", tmp_location.c_str(),
          written_files[kTestSink].begin()->file.c_str());
  fbl::unique_fd test_sink_fd(open(test_sink_location, O_RDWR));
  std::vector<uint8_t> test_sink_content;
  std::vector<uint8_t> expected(std::begin(kTestData), std::end(kTestData));
  files::ReadFileDescriptorToVector(test_sink_fd.get(), &test_sink_content);
  ASSERT_EQ(test_sink_content, expected);
}

// TODO: Add tests for merging profiles.

}  // namespace
