// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/gcs/cloud_storage_impl.h"

#include <memory>
#include <string>
#include <utility>

#include <fuchsia/net/oldhttp/cpp/fidl.h>

#include "gtest/gtest.h"
#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/network_wrapper/fake_network_wrapper.h"

namespace gcs {

namespace http = ::fuchsia::net::oldhttp;

namespace {

http::HttpHeaderPtr GetHeader(const fidl::VectorPtr<http::HttpHeader>& headers,
                              const std::string& header_name) {
  for (const auto& header : *headers) {
    if (header.name == header_name) {
      auto result = http::HttpHeader::New();
      fidl::Clone(header, result.get());
      return result;
    }
  }
  return nullptr;
}

class CloudStorageImplTest : public gtest::TestLoopFixture {
 public:
  CloudStorageImplTest()
      : fake_network_wrapper_(dispatcher()),
        gcs_(&fake_network_wrapper_, "project", "prefix") {}
  ~CloudStorageImplTest() override {}

 protected:
  void SetResponse(const std::string& body, int64_t content_length,
                   uint32_t status_code) {
    http::URLResponse server_response;
    server_response.body = http::URLBody::New();
    server_response.body->set_stream(fsl::WriteStringToSocket(body));
    server_response.status_code = status_code;

    http::HttpHeader content_length_header;
    content_length_header.name = "content-length";
    content_length_header.value = fxl::NumberToString(content_length);

    server_response.headers.push_back(std::move(content_length_header));

    fake_network_wrapper_.SetResponse(std::move(server_response));
  }

  bool CreateFile(const std::string& content, std::string* path) {
    if (!tmp_dir_.NewTempFile(path))
      return false;
    return files::WriteFile(*path, content.data(), content.size());
  }

  files::ScopedTempDir tmp_dir_;
  network_wrapper::FakeNetworkWrapper fake_network_wrapper_;
  CloudStorageImpl gcs_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(CloudStorageImplTest);
};

TEST_F(CloudStorageImplTest, TestUpload) {
  std::string content = "Hello World\n";
  fsl::SizedVmo data;
  ASSERT_TRUE(fsl::VmoFromString(content, &data));

  SetResponse("", 0, 200);
  bool called;
  Status status;
  gcs_.UploadObject(
      "", "hello-world", std::move(data),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(
      "https://firebasestorage.googleapis.com"
      "/v0/b/project.appspot.com/o/prefixhello-world",
      fake_network_wrapper_.GetRequest()->url);
  EXPECT_EQ("POST", fake_network_wrapper_.GetRequest()->method);
  EXPECT_TRUE(fake_network_wrapper_.GetRequest()->body->is_sized_buffer());
  std::string sent_content;
  EXPECT_TRUE(fsl::StringFromVmo(
      fake_network_wrapper_.GetRequest()->body->sized_buffer(), &sent_content));
  EXPECT_EQ(content, sent_content);

  http::HttpHeaderPtr content_length_header =
      GetHeader(fake_network_wrapper_.GetRequest()->headers, "content-length");
  EXPECT_TRUE(content_length_header);
  unsigned content_length;
  EXPECT_TRUE(fxl::StringToNumberWithError(content_length_header->value.get(),
                                           &content_length));
  EXPECT_EQ(content.size(), content_length);
}

TEST_F(CloudStorageImplTest, TestUploadAuth) {
  std::string content = "Hello World\n";
  fsl::SizedVmo data;
  ASSERT_TRUE(fsl::VmoFromString(content, &data));

  SetResponse("", 0, 200);
  bool called;
  Status status;
  gcs_.UploadObject(
      "this-is-a-token", "hello-world", std::move(data),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  http::HttpHeaderPtr authorization_header =
      GetHeader(fake_network_wrapper_.GetRequest()->headers, "authorization");
  EXPECT_TRUE(authorization_header);
  EXPECT_EQ("Bearer this-is-a-token", authorization_header->value);
}

TEST_F(CloudStorageImplTest, TestUploadWhenObjectAlreadyExists) {
  std::string content;
  fsl::SizedVmo data;
  ASSERT_TRUE(fsl::VmoFromString(content, &data));
  SetResponse("", 0, 412);

  bool called;
  Status status;
  gcs_.UploadObject(
      "", "hello-world", std::move(data),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OBJECT_ALREADY_EXISTS, status);
}

TEST_F(CloudStorageImplTest, TestDownload) {
  const std::string content = "Hello World\n";
  SetResponse(content, content.size(), 200);

  bool called;
  Status status;
  uint64_t size;
  zx::socket data;
  gcs_.DownloadObject("", "hello-world",
                      callback::Capture(callback::SetWhenCalled(&called),
                                        &status, &size, &data));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(
      "https://firebasestorage.googleapis.com"
      "/v0/b/project.appspot.com/o/prefixhello-world?alt=media",
      fake_network_wrapper_.GetRequest()->url);
  EXPECT_EQ("GET", fake_network_wrapper_.GetRequest()->method);

  std::string downloaded_content;
  EXPECT_TRUE(fsl::BlockingCopyToString(std::move(data), &downloaded_content));
  EXPECT_EQ(downloaded_content, content);
  EXPECT_EQ(size, content.size());
}

TEST_F(CloudStorageImplTest, TestDownloadAuth) {
  const std::string content = "Hello World\n";
  SetResponse(content, content.size(), 200);

  bool called;
  Status status;
  uint64_t size;
  zx::socket data;
  gcs_.DownloadObject("this-is-a-token", "hello-world",
                      callback::Capture(callback::SetWhenCalled(&called),
                                        &status, &size, &data));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  http::HttpHeaderPtr authorization_header =
      GetHeader(fake_network_wrapper_.GetRequest()->headers, "authorization");
  EXPECT_TRUE(authorization_header);
  EXPECT_EQ("Bearer this-is-a-token", authorization_header->value);
}

TEST_F(CloudStorageImplTest, TestDownloadNotFound) {
  SetResponse("", 0, 404);

  bool called;
  Status status;
  uint64_t size;
  zx::socket data;
  gcs_.DownloadObject("", "whoa",
                      callback::Capture(callback::SetWhenCalled(&called),
                                        &status, &size, &data));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::NOT_FOUND, status);
  EXPECT_EQ(
      "https://firebasestorage.googleapis.com"
      "/v0/b/project.appspot.com/o/prefixwhoa?alt=media",
      fake_network_wrapper_.GetRequest()->url);
  EXPECT_EQ("GET", fake_network_wrapper_.GetRequest()->method);
}

TEST_F(CloudStorageImplTest, TestDownloadWithResponseBodyTooShort) {
  const std::string content = "abc";
  SetResponse(content, content.size() + 1, 200);

  bool called;
  Status status;
  uint64_t size;
  zx::socket data;
  gcs_.DownloadObject("", "hello-world",
                      callback::Capture(callback::SetWhenCalled(&called),
                                        &status, &size, &data));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
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
