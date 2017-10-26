// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/gcs/cloud_storage_impl.h"

#include <memory>
#include <string>
#include <utility>

#include "gtest/gtest.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/network/fidl/network_service.fidl.h"
#include "peridot/bin/ledger/callback/capture.h"
#include "peridot/bin/ledger/network/fake_network_service.h"
#include "peridot/bin/ledger/test/test_with_message_loop.h"

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
        gcs_(message_loop_.task_runner(),
             &fake_network_service_,
             "project",
             "prefix") {}
  ~CloudStorageImplTest() override {}

 protected:
  void SetResponse(const std::string& body,
                   int64_t content_length,
                   uint32_t status_code) {
    network::URLResponsePtr server_response = network::URLResponse::New();
    server_response->body = network::URLBody::New();
    server_response->body->set_stream(fsl::WriteStringToSocket(body));
    server_response->status_code = status_code;

    network::HttpHeaderPtr content_length_header = network::HttpHeader::New();
    content_length_header->name = "content-length";
    content_length_header->value = fxl::NumberToString(content_length);

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
  FXL_DISALLOW_COPY_AND_ASSIGN(CloudStorageImplTest);
};

TEST_F(CloudStorageImplTest, TestUpload) {
  std::string content = "Hello World\n";
  zx::vmo data;
  ASSERT_TRUE(fsl::VmoFromString(content, &data));

  SetResponse("", 0, 200);
  Status status;
  gcs_.UploadObject("", "hello-world", std::move(data),
                    callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(
      "https://firebasestorage.googleapis.com"
      "/v0/b/project.appspot.com/o/prefixhello-world",
      fake_network_service_.GetRequest()->url);
  EXPECT_EQ("POST", fake_network_service_.GetRequest()->method);
  EXPECT_TRUE(fake_network_service_.GetRequest()->body->is_buffer());
  std::string sent_content;
  EXPECT_TRUE(fsl::StringFromVmo(
      fake_network_service_.GetRequest()->body->get_buffer(), &sent_content));
  EXPECT_EQ(content, sent_content);

  network::HttpHeaderPtr content_length_header =
      GetHeader(fake_network_service_.GetRequest()->headers, "content-length");
  EXPECT_TRUE(content_length_header);
  unsigned content_length;
  EXPECT_TRUE(fxl::StringToNumberWithError(content_length_header->value.get(),
                                           &content_length));
  EXPECT_EQ(content.size(), content_length);
}

TEST_F(CloudStorageImplTest, TestUploadAuth) {
  std::string content = "Hello World\n";
  zx::vmo data;
  ASSERT_TRUE(fsl::VmoFromString(content, &data));

  SetResponse("", 0, 200);
  Status status;
  gcs_.UploadObject("this-is-a-token", "hello-world", std::move(data),
                    callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());

  network::HttpHeaderPtr authorization_header =
      GetHeader(fake_network_service_.GetRequest()->headers, "authorization");
  EXPECT_TRUE(authorization_header);
  EXPECT_EQ("Bearer this-is-a-token", authorization_header->value);
}

TEST_F(CloudStorageImplTest, TestUploadWhenObjectAlreadyExists) {
  std::string content;
  zx::vmo data;
  ASSERT_TRUE(fsl::VmoFromString(content, &data));
  SetResponse("", 0, 412);

  Status status;
  gcs_.UploadObject("", "hello-world", std::move(data),
                    callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(Status::OBJECT_ALREADY_EXISTS, status);
}

TEST_F(CloudStorageImplTest, TestDownload) {
  const std::string content = "Hello World\n";
  SetResponse(content, content.size(), 200);

  Status status;
  uint64_t size;
  zx::socket data;
  gcs_.DownloadObject("", "hello-world",
                      callback::Capture(MakeQuitTask(), &status, &size, &data));
  ASSERT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(
      "https://firebasestorage.googleapis.com"
      "/v0/b/project.appspot.com/o/prefixhello-world?alt=media",
      fake_network_service_.GetRequest()->url);
  EXPECT_EQ("GET", fake_network_service_.GetRequest()->method);

  std::string downloaded_content;
  EXPECT_TRUE(fsl::BlockingCopyToString(std::move(data), &downloaded_content));
  EXPECT_EQ(downloaded_content, content);
  EXPECT_EQ(size, content.size());
}

TEST_F(CloudStorageImplTest, TestDownloadAuth) {
  const std::string content = "Hello World\n";
  SetResponse(content, content.size(), 200);

  Status status;
  uint64_t size;
  zx::socket data;
  gcs_.DownloadObject("this-is-a-token", "hello-world",
                      callback::Capture(MakeQuitTask(), &status, &size, &data));
  ASSERT_FALSE(RunLoopWithTimeout());

  network::HttpHeaderPtr authorization_header =
      GetHeader(fake_network_service_.GetRequest()->headers, "authorization");
  EXPECT_TRUE(authorization_header);
  EXPECT_EQ("Bearer this-is-a-token", authorization_header->value);
}

TEST_F(CloudStorageImplTest, TestDownloadNotFound) {
  SetResponse("", 0, 404);

  Status status;
  uint64_t size;
  zx::socket data;
  gcs_.DownloadObject("", "whoa",
                      callback::Capture(MakeQuitTask(), &status, &size, &data));
  ASSERT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(Status::NOT_FOUND, status);
  EXPECT_EQ(
      "https://firebasestorage.googleapis.com"
      "/v0/b/project.appspot.com/o/prefixwhoa?alt=media",
      fake_network_service_.GetRequest()->url);
  EXPECT_EQ("GET", fake_network_service_.GetRequest()->method);
}

TEST_F(CloudStorageImplTest, TestDownloadWithResponseBodyTooShort) {
  const std::string content = "abc";
  SetResponse(content, content.size() + 1, 200);

  Status status;
  uint64_t size;
  zx::socket data;
  gcs_.DownloadObject("", "hello-world",
                      callback::Capture(MakeQuitTask(), &status, &size, &data));
  ASSERT_FALSE(RunLoopWithTimeout());

  std::string downloaded_content;
  EXPECT_TRUE(fsl::BlockingCopyToString(std::move(data), &downloaded_content));

  // As the result is returned in a socket, we pass the expected size to the
  // client so that they can verify if the response is complete.
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(4u, size);
  EXPECT_EQ(3u, downloaded_content.size());
}

}  // namespace
}  // namespace gcs
