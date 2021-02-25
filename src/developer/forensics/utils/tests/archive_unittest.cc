// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/archive.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <map>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/gmatchers.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/vector.h"

namespace forensics {
namespace {

using fuchsia::feedback::Attachment;
using fuchsia::mem::Buffer;
using testing::Pair;
using testing::UnorderedElementsAreArray;

constexpr char kPlainTextFilename[] = "filename.txt";
constexpr char kJsonFilename[] = "filename.json";
constexpr char kXmlFilename[] = "filename.xml";
constexpr char kPlainTextFileContent[] = "plain text content";
constexpr char kJsonFileContent[] = R"({
  "key": "json content"
})";
constexpr char kXmlFileContent[] = "<tag>xml content</tag>";

// This corresponds to the content of resources/test_data.zip
const std::map<std::string, std::string> kAttachments = {
    {kPlainTextFilename, kPlainTextFileContent},
    {kJsonFilename, kJsonFileContent},
    {kXmlFilename, kXmlFileContent},
};

TEST(ArchiveTest, Archive) {
  fsl::SizedVmo archive;
  ASSERT_TRUE(Archive(kAttachments, &archive));
  ASSERT_GT(archive.size(), 0u);

  fsl::SizedVmo expected_vmo;
  ASSERT_TRUE(fsl::VmoFromFilename("/pkg/data/test_data.zip", &expected_vmo));
  std::vector<uint8_t> expected_bytes;
  ASSERT_TRUE(fsl::VectorFromVmo(expected_vmo, &expected_bytes));
  std::vector<uint8_t> actual_bytes;
  ASSERT_TRUE(fsl::VectorFromVmo(archive, &actual_bytes));
  EXPECT_EQ(actual_bytes, expected_bytes);
}

TEST(ArchiveTest, Unpack) {
  fsl::SizedVmo vmo;
  ASSERT_TRUE(fsl::VmoFromFilename("/pkg/data/test_data.zip", &vmo));
  Buffer archive = std::move(vmo).ToTransport();

  std::map<std::string, std::string> unpacked_attachments;
  ASSERT_TRUE(Unpack(archive, &unpacked_attachments));
  EXPECT_THAT(unpacked_attachments, UnorderedElementsAreArray({
                                        Pair(kPlainTextFilename, kPlainTextFileContent),
                                        Pair(kJsonFilename, kJsonFileContent),
                                        Pair(kXmlFilename, kXmlFileContent),
                                    }));
}

TEST(ArchiveTest, UnpackArchive) {
  fsl::SizedVmo archive;
  ASSERT_TRUE(Archive(kAttachments, &archive));

  std::map<std::string, std::string> unpacked_attachments;
  ASSERT_TRUE(Unpack(std::move(archive).ToTransport(), &unpacked_attachments));
  EXPECT_EQ(unpacked_attachments, kAttachments);
}

}  // namespace
}  // namespace forensics
