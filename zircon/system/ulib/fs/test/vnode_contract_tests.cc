// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/errors.h>

#include <type_traits>
#include <utility>

#include <fs/vfs_types.h>
#include <fs/vnode.h>
#include <zxtest/zxtest.h>

// "Vnode Contract Tests" verifies the runtime consistency checks enforced by the vnode APIs.
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
  bool IsDirectory() const final { ZX_PANIC("Unused"); }
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

}  // namespace
