// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/gcs/cloud_storage_impl.h"

#include <memory>
#include <string>
#include <utility>

#include "apps/ledger/src/network/fake_network_service.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "apps/network/services/network_service.fidl.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/mtl/socket/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/vmo/strings.h"

namespace gcs {
namespace {

network::HttpHeaderPtr GetHeader(
    const fidl::Array<network::HttpHeaderPtr>& headers,
    const std::string& header_name) {
  for (const auto& header : headers.storage()) {
    if (header->name == header_name) {
      return header.Clone();
    }
  }
  return nullptr;
}

class CloudStorageImplTest : public test::TestWithMessageLoop {
 public:
  CloudStorageImplTest()
      : fake_network_service_(message_loop_.task_runner()),
        gcs_(message_loop_.task_runner(), &fake_network_service_, "bucket") {}
  ~CloudStorageImplTest() override {}

 protected:
  void SetResponse(const std::string& body,
                   int64_t content_length,
                   uint32_t status_code) {
    network::URLResponsePtr server_response = network::URLResponse::New();
    server_response->body = network::URLBody::New();
    server_response->body->set_stream(mtl::WriteStringToSocket(body));
    server_response->status_code = status_code;

    network::HttpHeaderPtr content_length_header = network::HttpHeader::New();
    content_length_header->name = "content-length";
    content_length_header->value = ftl::NumberToString(content_length);

    server_response->headers.push_back(std::move(content_length_header));

    fake_network_service_.SetResponse(std::move(server_response));
  }

  bool CreateFile(const std::string& content, std::string* path) {
    if (!tmp_dir_.NewTempFile(path))
      return false;
    return files::WriteFile(*path, content.data(), content.size());
  }

  files::ScopedTempDir tmp_dir_;
  ledger::FakeNetworkService fake_network_service_;
  CloudStorageImpl gcs_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(CloudStorageImplTest);
};

TEST_F(CloudStorageImplTest, TestUpload) {
  const std::string content = "Hello World\n";
  std::string file;
  Status status;
  ASSERT_TRUE(CreateFile(content, &file));

  SetResponse("", 0, 200);
  gcs_.UploadFile("hello/world/baz/quz", file, [this, &status](Status s) {
    status = s;
    message_loop_.PostQuitTask();
  });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ("https://storage-upload.googleapis.com/bucket/hello/world/baz/quz",
            fake_network_service_.GetRequest()->url);
  EXPECT_EQ("PUT", fake_network_service_.GetRequest()->method);
  EXPECT_TRUE(fake_network_service_.GetRequest()->body->is_buffer());
  std::string sent_content;
  EXPECT_TRUE(mtl::StringFromVmo(
      std::move(fake_network_service_.GetRequest()->body->get_buffer()),
      &sent_content));
  EXPECT_EQ(content, sent_content);

  network::HttpHeaderPtr content_length_header =
      GetHeader(fake_network_service_.GetRequest()->headers, "content-length");
  EXPECT_TRUE(content_length_header);
  unsigned content_length;
  EXPECT_TRUE(ftl::StringToNumberWithError(content_length_header->value.get(),
                                           &content_length));
  EXPECT_EQ(content.size(), content_length);

  network::HttpHeaderPtr if_generation_match_header =
      GetHeader(fake_network_service_.GetRequest()->headers,
                "x-goog-if-generation-match");
  EXPECT_TRUE(if_generation_match_header);
  EXPECT_EQ("0", if_generation_match_header->value);
}

TEST_F(CloudStorageImplTest, TestUploadWhenObjectAlreadyExists) {
  std::string file;
  Status status;
  ASSERT_TRUE(CreateFile("", &file));

  SetResponse("", 0, 412);
  gcs_.UploadFile("hello/world/baz/quz", file, [this, &status](Status s) {
    status = s;
    message_loop_.PostQuitTask();
  });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(Status::OBJECT_ALREADY_EXIST, status);
}

TEST_F(CloudStorageImplTest, TestDownload) {
  const std::string content = "Hello World\n";
  std::string file;
  Status status;
  ASSERT_TRUE(CreateFile("", &file));

  SetResponse(content, content.size(), 200);
  gcs_.DownloadFile("hello/world/baz/quz", file, [this, &status](Status s) {
    status = s;
    message_loop_.PostQuitTask();
  });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(
      "https://storage-download.googleapis.com/bucket/hello/world/baz/quz",
      fake_network_service_.GetRequest()->url);
  EXPECT_EQ("GET", fake_network_service_.GetRequest()->method);

  std::string downloaded_content;
  EXPECT_TRUE(files::ReadFileToString(file, &downloaded_content));
  EXPECT_EQ(content, downloaded_content);
}

TEST_F(CloudStorageImplTest, TestDownloadWithResponseBodyTooShort) {
  const std::string content = "Hello World\n";
  std::string file;
  Status status;
  ASSERT_TRUE(CreateFile(content, &file));

  SetResponse(content, content.size() - 1, 200);
  gcs_.DownloadFile("hello/world/baz/quz", file, [this, &status](Status s) {
    status = s;
    message_loop_.PostQuitTask();
  });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(Status::UNKNOWN_ERROR, status);
}

}  // namespace
}  // namespace gcs
