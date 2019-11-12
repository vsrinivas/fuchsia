// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <zircon/errors.h>

#include <optional>
#include <type_traits>
#include <utility>

#include <fs/vfs_types.h>
#include <fs/vnode.h>
#include <fs/synchronous_vfs.h>
#include <zxtest/zxtest.h>
#include "fbl/ref_ptr.h"

// "Vnode Contract Tests" verifies the runtime contracts enforced by the vnode APIs. They could be
// consistency checks or other invariants.
namespace {

// This vnode returns a file in |GetProtocols|, but a directory in |GetNodeInfoForProtocol|.
class ErraticVnode : public fs::Vnode {
 public:
  ErraticVnode() = default;
  fs::VnodeProtocolSet GetProtocols() const final {
    return fs::VnodeProtocol::kFile;
  }
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights,
                                     fs::VnodeRepresentation* info) final {
    EXPECT_EQ(protocol, fs::VnodeProtocol::kFile);
    *info = fs::VnodeRepresentation::Directory();
    return ZX_OK;
  }
};

TEST(Vnode, ProtocolShouldAgreeWithNodeInfo) {
  ErraticVnode vnode;
  if (ZX_DEBUG_ASSERT_IMPLEMENTED) {
    ASSERT_DEATH([&] {
      fs::VnodeRepresentation info;
      vnode.GetNodeInfo(fs::Rights::All(), &info);
    });
  }
}

// This vnode supports the connector, file, and directory protocol.
class PolymorphicVnode : public fs::Vnode {
 public:
  PolymorphicVnode() = default;
  PolymorphicVnode(fs::VnodeProtocolSet expected_candidate)
      : expected_candidate_(expected_candidate) {}
  fs::VnodeProtocolSet GetProtocols() const final {
    return fs::VnodeProtocol::kConnector | fs::VnodeProtocol::kFile | fs::VnodeProtocol::kDirectory;
  }
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol, fs::Rights,
                                     fs::VnodeRepresentation*) final {
    EXPECT_TRUE(false, "Should not be called");
    return ZX_ERR_INTERNAL;
  }
  fs::VnodeProtocol Negotiate(fs::VnodeProtocolSet protocols) const final {
    EXPECT_TRUE(expected_candidate_.has_value());
    if (expected_candidate_.has_value()) {
      EXPECT_EQ(protocols, expected_candidate_);
    }
    negotiate_called_ = true;
    return protocols.first().value();
  }
  zx_status_t ConnectService(zx::channel) final { return ZX_OK; }
  bool negotiate_called() const { return negotiate_called_; }
 private:
  mutable bool negotiate_called_ = false;
  std::optional<fs::VnodeProtocolSet> expected_candidate_;
};

TEST(Vnode, NegotiateIsCalledIfMultipleCandidateProtocols) {
  auto vnode = fbl::MakeRefCounted<PolymorphicVnode>(fs::VnodeProtocol::kConnector |
                                                     fs::VnodeProtocol::kFile);
  fs::SynchronousVfs vfs;
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));

  ASSERT_FALSE(vnode->negotiate_called());
  ASSERT_OK(vfs.Serve(vnode, std::move(server_end),
                      fs::VnodeConnectionOptions::ReadOnly().set_not_directory()));
  ASSERT_TRUE(vnode->negotiate_called());
}

TEST(Vnode, NegotiateIsNotCalledIfSingleCandidateProtocol) {
  auto vnode = fbl::MakeRefCounted<PolymorphicVnode>();
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fs::SynchronousVfs vfs(loop.dispatcher());
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));

  ASSERT_FALSE(vnode->negotiate_called());
  ASSERT_OK(vfs.Serve(vnode, std::move(server_end),
                      fs::VnodeConnectionOptions::ReadOnly().set_directory()));
  ASSERT_FALSE(vnode->negotiate_called());
}

}  // namespace
