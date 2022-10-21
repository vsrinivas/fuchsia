// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/modular/lib/session/session.h"

#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/service.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/modular/lib/pseudo_dir/pseudo_dir_server.h"
#include "src/modular/lib/session/session_constants.h"

class SessionTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override { ASSERT_EQ(ZX_OK, fdio_ns_get_installed(&ns_)); }

  void TearDown() override {
    for (const auto& path : bound_ns_paths_) {
      ASSERT_EQ(ZX_OK, fdio_ns_unbind(ns_, path.c_str()));
    }
  }

  // Binds the |path| in the current process namespace to directory |handle|.
  void BindNamespacePath(std::string path, zx::handle handle) {
    ASSERT_EQ(ZX_OK, fdio_ns_bind(ns_, path.c_str(), handle.release()));
    bound_ns_paths_.emplace_back(path);
  }

  // Serves a protocol at the given |path|.
  template <typename Interface>
  void ServeProtocolAt(std::string_view path, fidl::InterfaceRequestHandler<Interface> handler) {
    // Split the path into two parts: a path to a directory, and the last segment,
    // an entry in that directory.
    auto path_split = fxl::SplitStringCopy(path, "/", fxl::WhiteSpaceHandling::kKeepWhitespace,
                                           fxl::SplitResult::kSplitWantNonEmpty);
    FX_CHECK(!path_split.empty());

    auto entry_name = std::move(path_split.back());
    path_split.pop_back();
    auto namespace_path = files::JoinPath("/", fxl::JoinStrings(path_split, "/"));

    auto new_protocol_server =
        std::make_unique<modular::PseudoDirServer>(std::make_unique<vfs::PseudoDir>());

    const auto& [it, inserted] =
        protocol_servers_.try_emplace(namespace_path, std::move(new_protocol_server));

    auto& protocol_server = it->second;
    auto dir = protocol_server->pseudo_dir();
    ASSERT_EQ(ZX_OK, dir->AddEntry(entry_name, std::make_unique<vfs::Service>(std::move(handler))));

    if (inserted) {
      BindNamespacePath(namespace_path, protocol_server->Serve().Unbind().TakeChannel());
    }
  }

 private:
  fdio_ns_t* ns_ = nullptr;
  std::vector<std::string> bound_ns_paths_;
  std::map<std::string, std::unique_ptr<modular::PseudoDirServer>> protocol_servers_;
};

// Tests that |ConnectToBasemgrDebug| can connect to |BasemgrDebug| served under
// the hub-v2 path that exists when basemgr is running as a v2 session.
TEST_F(SessionTest, ConnectToBasemgrDebugV2Session) {
  static constexpr auto kTestBasemgrDebugPath =
      "/hub-v2/children/core/children/session-manager/children/session:session/"
      "exec/expose/fuchsia.modular.internal.BasemgrDebug";

  // Serve the |BasemgrDebug| service in the process namespace at the path
  // |kTestBasemgrDebugPath|.
  bool got_request{false};
  fidl::InterfaceRequestHandler<fuchsia::modular::internal::BasemgrDebug> handler =
      [&](fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug> request) {
        got_request = true;
      };
  ServeProtocolAt<fuchsia::modular::internal::BasemgrDebug>(kTestBasemgrDebugPath,
                                                            std::move(handler));

  // Connect to the |BasemgrDebug| service.
  auto result = modular::session::ConnectToBasemgrDebug();
  EXPECT_TRUE(result.is_ok());

  // Ensure that the proxy returned is connected to the instance served above.
  fuchsia::modular::internal::BasemgrDebugPtr basemgr_debug = result.take_value();
  basemgr_debug->StartSessionWithRandomId();

  RunLoopUntil([&]() { return got_request; });
  EXPECT_TRUE(got_request);
}
