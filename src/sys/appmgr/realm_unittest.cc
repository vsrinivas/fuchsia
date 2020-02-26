// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/realm.h"

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <unistd.h>

#include "gtest/gtest.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/strings/substitute.h"

namespace component {
namespace {

class RealmTest : public ::testing::Test {
 protected:
  std::string NewFile(const std::string& base_dir, const std::string& path,
                      const std::string& contents) {
    const std::string file = fxl::Substitute("$0/$1", base_dir, path);
    if (!files::WriteFile(file, contents.data(), contents.size())) {
      return "";
    }
    return file;
  }

  std::unique_ptr<Realm> CreateTestRealm(fxl::UniqueFD dirfd) {
    // Make a stub scheme_map/ dir under the config dir, since Realm wants that
    // folder to exist.
    files::CreateDirectoryAt(dirfd.get(), "scheme_map");
    auto environment_services = sys::ServiceDirectory::CreateFromNamespace();
    fuchsia::sys::ServiceListPtr root_realm_services(new fuchsia::sys::ServiceList);
    RealmArgs realm_args = RealmArgs::MakeWithAdditionalServices(
        nullptr, "test", "/data", "/data/cache", "/tmp", std::move(environment_services), false,
        std::move(root_realm_services), fuchsia::sys::EnvironmentOptions{}, std::move(dirfd));
    return Realm::Create(std::move(realm_args));
  }

  files::ScopedTempDir tmp_dir_;
};

TEST_F(RealmTest, ReplaceAsExecPolicyPresent) {
  static constexpr char kFile[] = R"F(
  fuchsia-pkg://fuchsia.com/chromium#meta/chromium.cmx
  )F";

  // Stub out a dispatcher.  We won't actually run anything on it, but some
  // things in Realm assert they can grab the implicit default eventloop, so
  // keep them happy.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));

  // Add the allowlist.
  ASSERT_TRUE(files::CreateDirectoryAt(dirfd.get(), "allowlist"));
  auto filename = NewFile(dir, "allowlist/deprecated_ambient_replace_as_executable.txt", kFile);
  auto realm = CreateTestRealm(std::move(dirfd));

  EXPECT_TRUE(realm->IsAllowedToUseDeprecatedAmbientReplaceAsExecutable(
      "fuchsia-pkg://fuchsia.com/chromium#meta/chromium.cmx"));
  EXPECT_FALSE(realm->IsAllowedToUseDeprecatedAmbientReplaceAsExecutable(
      "fuchsia-pkg://fuchsia.com/stash#meta/stash.cmx"));
}

TEST_F(RealmTest, ReplaceAsExecPolicyAbsent) {
  // Stub out a dispatcher.  We won't actually run anything on it, but some
  // things in Realm assert they can grab the implicit default eventloop, so
  // keep them happy.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));

  // No allowlist present in this test.

  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));
  auto realm = CreateTestRealm(std::move(dirfd));

  EXPECT_TRUE(realm->IsAllowedToUseDeprecatedAmbientReplaceAsExecutable(
      "fuchsia-pkg://fuchsia.com/chromium#meta/chromium.cmx"));
  EXPECT_TRUE(realm->IsAllowedToUseDeprecatedAmbientReplaceAsExecutable(
      "fuchsia-pkg://fuchsia.com/stash#meta/stash.cmx"));
}

TEST_F(RealmTest, PackageResolverPolicy) {
  static constexpr char kFile[] = R"F(
  fuchsia-pkg://fuchsia.com/amber#meta/system_updater.cmx
  )F";

  // Stub out a dispatcher.  We won't actually run anything on it, but some
  // things in Realm assert they can grab the implicit default eventloop, so
  // keep them happy.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));

  // Add the allowlist.
  ASSERT_TRUE(files::CreateDirectoryAt(dirfd.get(), "allowlist"));
  auto filename = NewFile(dir, "allowlist/package_resolver.txt", kFile);
  auto realm = CreateTestRealm(std::move(dirfd));

  FuchsiaPkgUrl fp;

  // "Vanilla" package url, without variant or hash
  fp.Parse("fuchsia-pkg://fuchsia.com/amber#meta/system_updater.cmx");
  EXPECT_TRUE(realm->IsAllowedToUsePackageResolver(fp.WithoutVariantAndHash()));

  // Variants and hashes should be thrown away
  fp.Parse("fuchsia-pkg://fuchsia.com/amber/0?hash=123#meta/system_updater.cmx");
  EXPECT_TRUE(realm->IsAllowedToUsePackageResolver(fp.WithoutVariantAndHash()));

  // Check exclusion
  fp.Parse("fuchsia-pkg://fuchsia.com/stash#meta/stash.cmx");
  EXPECT_FALSE(realm->IsAllowedToUsePackageResolver(fp.WithoutVariantAndHash()));
}

}  // namespace
}  // namespace component
