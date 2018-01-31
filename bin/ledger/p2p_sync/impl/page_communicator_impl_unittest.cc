// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/p2p_sync/impl/page_communicator_impl.h"

#include "garnet/lib/gtest/test_with_message_loop.h"
// gtest matchers are in gmock and we cannot include the specific header file
// directly as it is private to the library.
#include "gmock/gmock.h"
#include "peridot/bin/ledger/p2p_sync/impl/device_mesh.h"
#include "peridot/lib/convert/convert.h"

namespace p2p_sync {
namespace {
class FakeDeviceMesh : public DeviceMesh {
 public:
  FakeDeviceMesh() {}

  const DeviceSet& GetDeviceList() override { return devices_; }

  void Send(fxl::StringView device_name, fxl::StringView data) override {
    messages_.emplace_back(
        std::forward_as_tuple(device_name.ToString(), data.ToString()));
  }

  DeviceSet devices_;
  std::vector<std::pair<std::string, std::string>> messages_;
};

class PageCommunicatorImplTest : public gtest::TestWithMessageLoop {
 public:
  PageCommunicatorImplTest() {}
  ~PageCommunicatorImplTest() override {}

 protected:
  void SetUp() override { ::testing::Test::SetUp(); }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageCommunicatorImplTest);
};

TEST_F(PageCommunicatorImplTest, ConnectToExistingMesh) {
  FakeDeviceMesh mesh;
  mesh.devices_.emplace("device2");
  PageCommunicatorImpl page_communicator("ledger", "page", &mesh);

  EXPECT_TRUE(mesh.messages_.empty());

  page_communicator.Start();

  ASSERT_EQ(1u, mesh.messages_.size());
  EXPECT_EQ("device2", mesh.messages_[0].first);

  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(mesh.messages_[0].second.data()),
      mesh.messages_[0].second.size());
  if (!VerifyMessageBuffer(verifier)) {
    // Wrong serialization, abort.
    FXL_LOG(ERROR) << "The message received is malformed.";
    return;
  };
  const Message* message = GetMessage(mesh.messages_[0].second.data());
  ASSERT_EQ(MessageUnion_Request, message->message_type());
  const Request* request = static_cast<const Request*>(message->message());
  const NamespacePageId* namespace_page_id = request->namespace_page();
  EXPECT_EQ("ledger",
            convert::ExtendedStringView(namespace_page_id->namespace_id()));
  EXPECT_EQ("page", convert::ExtendedStringView(namespace_page_id->page_id()));
  EXPECT_EQ(RequestMessage_WatchStartRequest, request->request_type());
}

TEST_F(PageCommunicatorImplTest, ConnectToNewMeshParticipant) {
  FakeDeviceMesh mesh;
  PageCommunicatorImpl page_communicator("ledger", "page", &mesh);
  page_communicator.Start();

  EXPECT_TRUE(mesh.messages_.empty());

  mesh.devices_.emplace("device2");
  page_communicator.OnDeviceChange("device2",
                                   p2p_provider::DeviceChangeType::NEW);

  ASSERT_EQ(1u, mesh.messages_.size());
  EXPECT_EQ("device2", mesh.messages_[0].first);

  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(mesh.messages_[0].second.data()),
      mesh.messages_[0].second.size());
  if (!VerifyMessageBuffer(verifier)) {
    // Wrong serialization, abort.
    FXL_LOG(ERROR) << "The message received is malformed.";
    return;
  };
  const Message* message = GetMessage(mesh.messages_[0].second.data());
  ASSERT_EQ(MessageUnion_Request, message->message_type());
  const Request* request = static_cast<const Request*>(message->message());
  const NamespacePageId* namespace_page_id = request->namespace_page();
  EXPECT_EQ("ledger",
            convert::ExtendedStringView(namespace_page_id->namespace_id()));
  EXPECT_EQ("page", convert::ExtendedStringView(namespace_page_id->page_id()));
  EXPECT_EQ(RequestMessage_WatchStartRequest, request->request_type());
}

}  // namespace
}  // namespace p2p_sync
