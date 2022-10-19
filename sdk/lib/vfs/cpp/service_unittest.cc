// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/vfs/cpp/service.h"

#include <lib/fdio/vfs.h>
#include <lib/fidl/cpp/binding_set.h>
#include <zircon/status.h>

#include <test/placeholders/cpp/fidl.h>

#include "fuchsia/io/cpp/fidl.h"
#include "lib/vfs/cpp/pseudo_dir.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

class ServiceTest : public gtest::RealLoopFixture, public test::placeholders::Echo {
  void EchoString(fidl::StringPtr value, EchoStringCallback callback) override {
    callback(answer_);
  }

 protected:
  ServiceTest()
      : answer_("my_fake_ans"),
        service_name_("echo_service"),
        second_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    auto service =
        std::make_unique<vfs::Service>(bindings_.GetHandler(this, second_loop_.dispatcher()));

    dir_.Serve(fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::RIGHT_WRITABLE,
               dir_ptr_.NewRequest().TakeChannel(), second_loop_.dispatcher());
    dir_.AddEntry(service_name_, std::move(service));
    second_loop_.StartThread("vfs test thread");
  }

  const std::string& answer() const { return answer_; }

  const std::string& service_name() const { return service_name_; }

  void ExpectOnOpen(fuchsia::io::OpenFlags flag, uint32_t mode, zx_status_t expected_status) {
    SCOPED_TRACE("flag: " + std::to_string(static_cast<uint32_t>(flag)) +
                 ", mode: " + std::to_string(mode));
    fuchsia::io::NodePtr node_ptr;

    bool on_open_called = false;

    node_ptr.events().OnOpen = [&](zx_status_t status,
                                   std::unique_ptr<fuchsia::io::NodeInfoDeprecated> unused) {
      EXPECT_FALSE(on_open_called);  // should be called only once
      on_open_called = true;
      EXPECT_EQ(expected_status, status);
    };

    dir_ptr()->Open(flag | fuchsia::io::OpenFlags::DESCRIBE, mode, service_name(),
                    node_ptr.NewRequest());
    RunLoopUntil([&]() { return on_open_called; }, zx::msec(1));
  }

  fuchsia::io::DirectoryPtr& dir_ptr() { return dir_ptr_; }

 private:
  std::string answer_;
  std::string service_name_;
  fidl::BindingSet<Echo> bindings_;
  vfs::PseudoDir dir_;
  fuchsia::io::DirectoryPtr dir_ptr_;
  async::Loop second_loop_;
};

TEST_F(ServiceTest, CanOpenAsNodeReferenceAndTestGetAttr) {
  fuchsia::io::NodeSyncPtr ptr;
  dir_ptr()->Open(fuchsia::io::OpenFlags::NODE_REFERENCE, 0, service_name(), ptr.NewRequest());

  zx_status_t s;
  fuchsia::io::NodeAttributes attr;
  ptr->GetAttr(&s, &attr);
  EXPECT_EQ(ZX_OK, s);
  EXPECT_EQ(fuchsia::io::MODE_TYPE_SERVICE, attr.mode & fuchsia::io::MODE_TYPE_SERVICE);
}

TEST_F(ServiceTest, CanCloneNodeReference) {
  fuchsia::io::NodeSyncPtr cloned_ptr;
  {
    fuchsia::io::NodeSyncPtr ptr;
    dir_ptr()->Open(fuchsia::io::OpenFlags::NODE_REFERENCE, 0, service_name(), ptr.NewRequest());

    ptr->Clone({}, cloned_ptr.NewRequest());
  }

  zx_status_t s;
  fuchsia::io::NodeAttributes attr;
  cloned_ptr->GetAttr(&s, &attr);
  EXPECT_EQ(ZX_OK, s);
  EXPECT_EQ(fuchsia::io::MODE_TYPE_SERVICE, attr.mode & fuchsia::io::MODE_TYPE_SERVICE);
}

TEST_F(ServiceTest, TestQuery) {
  fuchsia::io::NodeSyncPtr ptr;
  dir_ptr()->Open(fuchsia::io::OpenFlags::NODE_REFERENCE, 0, service_name(), ptr.NewRequest());

  std::vector<uint8_t> protocol;
  zx_status_t status = ptr->Query(&protocol);
  ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
  ASSERT_EQ(std::string(protocol.begin(), protocol.end()), fuchsia::io::NODE_PROTOCOL_NAME);
}

TEST_F(ServiceTest, CanOpenAsAService) {
  fuchsia::io::OpenFlags flags[] = {
      fuchsia::io::OpenFlags::RIGHT_READABLE, fuchsia::io::OpenFlags::RIGHT_WRITABLE,
      fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::NOT_DIRECTORY};
  uint32_t modes[] = {
      // Valid mode types:
      0,
      fuchsia::io::MODE_TYPE_SERVICE,
      V_IRWXU,
      V_IRUSR,
      V_IWUSR,
      V_IXUSR,
      // Invalid mode types (should be ignored as node already exists):
      // fuchsia::io::MODE_TYPE_BLOCK_DEVICE,
      fuchsia::io::MODE_TYPE_FILE,
  };

  for (uint32_t mode : modes) {
    for (fuchsia::io::OpenFlags flag : flags) {
      SCOPED_TRACE("flag: " + std::to_string(static_cast<uint32_t>(flag)) +
                   ", mode: " + std::to_string(mode));
      test::placeholders::EchoSyncPtr ptr;
      dir_ptr()->Open(flag, mode, service_name(),
                      fidl::InterfaceRequest<fuchsia::io::Node>(ptr.NewRequest().TakeChannel()));

      fidl::StringPtr ans;
      ptr->EchoString("hello", &ans);
      ASSERT_TRUE(ans.has_value());
      EXPECT_EQ(answer(), ans.value());
    }
  }
}

TEST_F(ServiceTest, OnOpenEvent) {
  // Ensure we get an OnOpen event when we open both the service or the node itself.
  ExpectOnOpen(fuchsia::io::OpenFlags::RIGHT_READABLE, 0, ZX_OK);
  ExpectOnOpen(fuchsia::io::OpenFlags::NODE_REFERENCE, 0, ZX_OK);
}

TEST_F(ServiceTest, CannotOpenServiceWithInvalidFlags) {
  fuchsia::io::OpenFlags flags[] = {
      fuchsia::io::OpenFlags::CREATE, fuchsia::io::OpenFlags::CREATE_IF_ABSENT,
      fuchsia::io::OpenFlags::TRUNCATE, fuchsia::io::OpenFlags::APPEND};

  for (fuchsia::io::OpenFlags flag : flags) {
    ExpectOnOpen(
        flag | fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::RIGHT_WRITABLE, 0,
        ZX_ERR_NOT_SUPPORTED);
  }
  ExpectOnOpen(fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::DIRECTORY, 0,
               ZX_ERR_NOT_DIR);
}
