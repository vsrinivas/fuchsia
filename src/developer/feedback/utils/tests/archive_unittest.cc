// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/archive.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>

#include <map>
#include <string>
#include <vector>

#include "src/developer/feedback/testing/gmatchers.h"
#include "src/developer/feedback/testing/gpretty_printers.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fsl/vmo/vector.h"
#include "src/lib/fxl/logging.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using fuchsia::feedback::Attachment;
using fuchsia::mem::Buffer;

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

Attachment BuildAttachment(const std::string& key, const std::string& value) {
  Attachment attachment;
  attachment.key = key;
  FXL_CHECK(fsl::VmoFromString(value, &attachment.value));
  return attachment;
}

std::vector<Attachment> BuildAttachments(
    const std::map<std::string, std::string>& str_attachments) {
  std::vector<Attachment> attachments;
  for (const auto& [key, value] : str_attachments) {
    attachments.push_back(BuildAttachment(key, value));
  }
  return attachments;
}

TEST(ArchiveTest, Archive) {
  Buffer archive;
  ASSERT_TRUE(Archive(BuildAttachments(kAttachments), &archive));
  ASSERT_TRUE(archive.vmo.is_valid());
  ASSERT_GT(archive.size, 0u);

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

  std::vector<Attachment> unpacked_attachments;
  ASSERT_TRUE(Unpack(archive, &unpacked_attachments));
  EXPECT_THAT(unpacked_attachments,
              testing::UnorderedElementsAreArray({
                  MatchesAttachment(kPlainTextFilename, kPlainTextFileContent),
                  MatchesAttachment(kJsonFilename, kJsonFileContent),
                  MatchesAttachment(kXmlFilename, kXmlFileContent),
              }));
}

TEST(ArchiveTest, UnpackArchive) {
  const std::vector<Attachment> original_attachments = BuildAttachments(kAttachments);
  Buffer archive;
  ASSERT_TRUE(Archive(original_attachments, &archive));

  std::vector<Attachment> unpacked_attachments;
  ASSERT_TRUE(Unpack(archive, &unpacked_attachments));
  EXPECT_THAT(unpacked_attachments,
              testing::UnorderedElementsAreArray({
                  MatchesAttachment(kPlainTextFilename, kPlainTextFileContent),
                  MatchesAttachment(kJsonFilename, kJsonFileContent),
                  MatchesAttachment(kXmlFilename, kXmlFileContent),
              }));
}

}  // namespace
}  // namespace feedback
