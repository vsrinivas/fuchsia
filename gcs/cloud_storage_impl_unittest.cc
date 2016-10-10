// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/gcs/cloud_storage_impl.h"

#include <memory>
#include <string>
#include <utility>

#include "apps/ledger/fake_network_service/fake_network_service.h"
#include "apps/network/interfaces/network_service.mojom.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/mtl/data_pipe/strings.h"
#include "lib/mtl/tasks/message_loop.h"

namespace gcs {
namespace {

mojo::HttpHeaderPtr GetHeader(const mojo::Array<mojo::HttpHeaderPtr>& headers,
                              const std::string& header_name) {
  for (const auto& header : headers.storage()) {
    if (header->name == header_name) {
      return header.Clone();
    }
  }
  return nullptr;
}

class CloudStorageImplTest : public ::testing::Test {
 public:
  CloudStorageImplTest() {}
  ~CloudStorageImplTest() override {}

 protected:
  // ApplicationTestBase:
  void SetUp() override {
    ::testing::Test::SetUp();

    mojo::NetworkServicePtr fake_network_service;
    fake_network_service_.reset(new fake_network_service::FakeNetworkService(
        GetProxy(&fake_network_service)));

    gcs_.reset(new CloudStorageImpl(message_loop_.task_runner(),
                                    std::move(fake_network_service), "bucket"));

    tmp_dir_.reset(new files::ScopedTempDir());
  }

  void SetResponse(const std::string& body,
                   int64_t content_length,
                   uint32_t status_code) {
    mojo::URLResponsePtr server_response = mojo::URLResponse::New();
    server_response->body = mtl::WriteStringToConsumerHandle(body);
    server_response->status_code = status_code;

    mojo::HttpHeaderPtr content_length_header = mojo::HttpHeader::New();
    content_length_header->name = "content-length";
    content_length_header->value = ftl::NumberToString(content_length);

    server_response->headers.push_back(std::move(content_length_header));

    fake_network_service_->SetResponse(std::move(server_response));
  }

  bool CreateFile(const std::string& content, std::string* path) {
    if (!tmp_dir_->NewTempFile(path))
      return false;
    return files::WriteFile(*path, content.data(), content.size());
  }

  mtl::MessageLoop message_loop_;
  std::unique_ptr<CloudStorageImpl> gcs_;
  std::unique_ptr<fake_network_service::FakeNetworkService>
      fake_network_service_;
  std::unique_ptr<files::ScopedTempDir> tmp_dir_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(CloudStorageImplTest);
};

TEST_F(CloudStorageImplTest, TestUpload) {
  const std::string content = "Hello World\n";
  std::string file;
  Status status;
  ASSERT_TRUE(CreateFile(content, &file));

  SetResponse("", 0, 200);
  gcs_->UploadFile("hello/world/baz/quz", file, [this, &status](Status s) {
    status = s;
    message_loop_.QuitNow();
  });
  message_loop_.Run();

  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ("https://storage-upload.googleapis.com/bucket/hello/world/baz/quz",
            fake_network_service_->GetRequest()->url);
  EXPECT_EQ("PUT", fake_network_service_->GetRequest()->method);
  EXPECT_EQ(1u, fake_network_service_->GetRequest()->body.size());
  std::string sent_content;
  EXPECT_TRUE(mtl::BlockingCopyToString(
      std::move(fake_network_service_->GetRequest()->body[0]), &sent_content));
  EXPECT_EQ(content, sent_content);

  mojo::HttpHeaderPtr content_length_header =
      GetHeader(fake_network_service_->GetRequest()->headers, "content-length");
  EXPECT_TRUE(content_length_header);
  unsigned content_length;
  EXPECT_TRUE(ftl::StringToNumberWithError(content_length_header->value.get(),
                                           &content_length));
  EXPECT_EQ(content.size(), content_length);

  mojo::HttpHeaderPtr if_generation_match_header =
      GetHeader(fake_network_service_->GetRequest()->headers,
                "x-goog-if-generation-match");
  EXPECT_TRUE(if_generation_match_header);
  EXPECT_EQ("0", if_generation_match_header->value);
}

TEST_F(CloudStorageImplTest, TestUploadWhenObjectAlreadyExists) {
  std::string file;
  Status status;
  ASSERT_TRUE(CreateFile("", &file));

  SetResponse("", 0, 412);
  gcs_->UploadFile("hello/world/baz/quz", file, [this, &status](Status s) {
    status = s;
    message_loop_.QuitNow();
  });
  message_loop_.Run();

  EXPECT_EQ(Status::OBJECT_ALREADY_EXIST, status);
}

TEST_F(CloudStorageImplTest, TestDownload) {
  const std::string content = "Hello World\n";
  std::string file;
  Status status;
  ASSERT_TRUE(CreateFile("", &file));

  SetResponse(content, content.size(), 200);
  gcs_->DownloadFile("hello/world/baz/quz", file, [this, &status](Status s) {
    status = s;
    message_loop_.QuitNow();
  });
  message_loop_.Run();

  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(
      "https://storage-download.googleapis.com/bucket/hello/world/baz/quz",
      fake_network_service_->GetRequest()->url);
  EXPECT_EQ("GET", fake_network_service_->GetRequest()->method);

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
  gcs_->DownloadFile("hello/world/baz/quz", file, [this, &status](Status s) {
    status = s;
    message_loop_.QuitNow();
  });
  message_loop_.Run();

  EXPECT_EQ(Status::UNKNOWN_ERROR, status);
}

}  // namespace
}  // namespace gcs
