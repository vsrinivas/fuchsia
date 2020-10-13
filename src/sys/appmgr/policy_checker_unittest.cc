// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/policy_checker.h"

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "src/lib/cmx/sandbox.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/strings/substitute.h"

namespace component {

class PolicyCheckerTest : public ::testing::Test {
 protected:
  std::string NewFile(const std::string& base_dir, const std::string& path,
                      const std::string& contents) {
    const std::string file = fxl::Substitute("$0/$1", base_dir, path);
    if (!files::WriteFile(file, contents.data(), contents.size())) {
      return "";
    }
    return file;
  }

  files::ScopedTempDir tmp_dir_;
};

TEST_F(PolicyCheckerTest, ReplaceAsExecPolicyPresent) {
  static constexpr char kFile[] = R"F(
  fuchsia-pkg://fuchsia.com/chromium#meta/chromium.cmx
  )F";

  // Stub out a dispatcher.  We won't actually run anything on it, but some
  // things in PolicyChecker assert they can grab the implicit default eventloop, so
  // keep them happy.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));

  // Add the allowlist.
  ASSERT_TRUE(files::CreateDirectoryAt(dirfd.get(), "allowlist"));
  auto filename = NewFile(dir, "allowlist/deprecated_ambient_replace_as_executable.txt", kFile);

  PolicyChecker policy_checker(std::move(dirfd));
  FuchsiaPkgUrl fp;
  fp.Parse("fuchsia-pkg://fuchsia.com/chromium#meta/chromium.cmx");
  EXPECT_TRUE(policy_checker.CheckDeprecatedAmbientReplaceAsExecutable(fp));
  fp.Parse("fuchsia-pkg://fuchsia.com/stash#meta/stash.cmx");
  EXPECT_FALSE(policy_checker.CheckDeprecatedAmbientReplaceAsExecutable(fp));
}

TEST_F(PolicyCheckerTest, ReplaceAsExecPolicyAbsent) {
  // Stub out a dispatcher.  We won't actually run anything on it, but some
  // things in PolicyChecker assert they can grab the implicit default eventloop, so
  // keep them happy.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));

  // No allowlist present in this test.  This means that all packages should be
  // disallowed ambient replace-as-exec.

  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));
  PolicyChecker policy_checker(std::move(dirfd));

  FuchsiaPkgUrl fp;
  fp.Parse("fuchsia-pkg://fuchsia.com/chromium#meta/chromium.cmx");
  EXPECT_FALSE(policy_checker.CheckDeprecatedAmbientReplaceAsExecutable(fp));
  fp.Parse("fuchsia-pkg://fuchsia.com/stash#meta/stash.cmx");
  EXPECT_FALSE(policy_checker.CheckDeprecatedAmbientReplaceAsExecutable(fp));
}

TEST_F(PolicyCheckerTest, DurableDataPolicy) {
  static constexpr char kFile[] = R"F(
fuchsia-pkg://fuchsia.com/indubitably-durable#meta/indubitably_durable.cmx
)F";

  SandboxMetadata sandbox{};
  sandbox.AddFeature("durable-data");

  // Stub out a dispatcher.  We won't actually run anything on it, but some
  // things in PolicyChecker assert they can grab the implicit default eventloop, so
  // keep them happy.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));

  // Add the allowlist.
  ASSERT_TRUE(files::CreateDirectoryAt(dirfd.get(), "allowlist"));
  auto filename = NewFile(dir, "allowlist/durable_data.txt", kFile);

  FuchsiaPkgUrl fp;
  PolicyChecker policy_checker(std::move(dirfd));

  // "Vanilla" package url, without variant or hash
  fp.Parse("fuchsia-pkg://fuchsia.com/indubitably-durable#meta/indubitably_durable.cmx");
  EXPECT_TRUE(policy_checker.Check(sandbox, fp));

  // Variants and hashes should be thrown away
  fp.Parse("fuchsia-pkg://fuchsia.com/indubitably-durable/0?hash=123#meta/indubitably_durable.cmx");
  EXPECT_TRUE(policy_checker.Check(sandbox, fp));

  // Check exclusion
  fp.Parse("fuchsia-pkg://fuchsia.com/stash#meta/stash.cmx");
  EXPECT_FALSE(policy_checker.Check(sandbox, fp));
}

TEST_F(PolicyCheckerTest, FactoryDataPolicy) {
  static constexpr char kFile[] = R"F(
fuchsia-pkg://fuchsia.com/fastidious-factory#meta/fastidious_factory.cmx
)F";

  SandboxMetadata sandbox{};
  sandbox.AddFeature("factory-data");

  // Stub out a dispatcher.  We won't actually run anything on it, but some
  // things in PolicyChecker assert they can grab the implicit default eventloop, so
  // keep them happy.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));

  // Add the allowlist.
  ASSERT_TRUE(files::CreateDirectoryAt(dirfd.get(), "allowlist"));
  auto filename = NewFile(dir, "allowlist/factory_data.txt", kFile);

  FuchsiaPkgUrl fp;
  PolicyChecker policy_checker(std::move(dirfd));

  // "Vanilla" package url, without variant or hash
  fp.Parse("fuchsia-pkg://fuchsia.com/fastidious-factory#meta/fastidious_factory.cmx");
  EXPECT_TRUE(policy_checker.Check(sandbox, fp));

  // Variants and hashes should be thrown away
  fp.Parse("fuchsia-pkg://fuchsia.com/fastidious-factory/0?hash=123#meta/fastidious_factory.cmx");
  EXPECT_TRUE(policy_checker.Check(sandbox, fp));

  // Check exclusion
  fp.Parse("fuchsia-pkg://fuchsia.com/stash#meta/stash.cmx");
  EXPECT_FALSE(policy_checker.Check(sandbox, fp));
}

TEST_F(PolicyCheckerTest, HubPolicy) {
  static constexpr char kFile[] = R"F(
  fuchsia-pkg://fuchsia.com/terminal#meta/terminal.cmx
  )F";

  // Stub out a dispatcher.  We won't actually run anything on it, but some
  // things in PolicyChecker assert they can grab the implicit default eventloop, so
  // keep them happy.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));

  // Add the allowlist.
  ASSERT_TRUE(files::CreateDirectoryAt(dirfd.get(), "allowlist"));
  auto filename = NewFile(dir, "allowlist/hub.txt", kFile);

  FuchsiaPkgUrl fp;
  PolicyChecker policy_checker(std::move(dirfd));

  // "Vanilla" package url, without variant or hash
  fp.Parse("fuchsia-pkg://fuchsia.com/terminal#meta/terminal.cmx");
  EXPECT_TRUE(policy_checker.CheckHub(fp));

  // Variants and hashes should be thrown away
  fp.Parse("fuchsia-pkg://fuchsia.com/terminal/0?hash=123#meta/terminal.cmx");
  EXPECT_TRUE(policy_checker.CheckHub(fp));

  // Check exclusion
  fp.Parse("fuchsia-pkg://fuchsia.com/stash#meta/stash.cmx");
  EXPECT_FALSE(policy_checker.CheckHub(fp));
}

TEST_F(PolicyCheckerTest, MmioResourcePolicy) {
  static constexpr char kFile[] = R"F(
  fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx
  )F";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));

  // Add the allowlist.
  ASSERT_TRUE(files::CreateDirectoryAt(dirfd.get(), "allowlist"));
  auto filename = NewFile(dir, "allowlist/mmio_resource.txt", kFile);

  FuchsiaPkgUrl fp;
  PolicyChecker policy_checker(std::move(dirfd));

  // "Vanilla" package url, without variant or hash
  fp.Parse("fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx");
  EXPECT_TRUE(policy_checker.CheckMmioResource(fp));

  // Variants and hashes should be thrown away
  fp.Parse("fuchsia-pkg://fuchsia.com/foo/0?hash=123#meta/foo.cmx");
  EXPECT_TRUE(policy_checker.CheckMmioResource(fp));

  // Check exclusion
  fp.Parse("fuchsia-pkg://fuchsia.com/bar#meta/bar.cmx");
  EXPECT_FALSE(policy_checker.CheckMmioResource(fp));
}

TEST_F(PolicyCheckerTest, PackageResolverPolicy) {
  static constexpr char kFile[] = R"F(
  fuchsia-pkg://fuchsia.com/amber#meta/system_updater.cmx
  )F";

  // Stub out a dispatcher.  We won't actually run anything on it, but some
  // things in PolicyChecker assert they can grab the implicit default eventloop, so
  // keep them happy.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));

  // Add the allowlist.
  ASSERT_TRUE(files::CreateDirectoryAt(dirfd.get(), "allowlist"));
  auto filename = NewFile(dir, "allowlist/package_resolver.txt", kFile);

  FuchsiaPkgUrl fp;
  PolicyChecker policy_checker(std::move(dirfd));

  // "Vanilla" package url, without variant or hash
  fp.Parse("fuchsia-pkg://fuchsia.com/amber#meta/system_updater.cmx");
  EXPECT_TRUE(policy_checker.CheckPackageResolver(fp));

  // Variants and hashes should be thrown away
  fp.Parse("fuchsia-pkg://fuchsia.com/amber/0?hash=123#meta/system_updater.cmx");
  EXPECT_TRUE(policy_checker.CheckPackageResolver(fp));

  // Check exclusion
  fp.Parse("fuchsia-pkg://fuchsia.com/stash#meta/stash.cmx");
  EXPECT_FALSE(policy_checker.CheckPackageResolver(fp));
}

TEST_F(PolicyCheckerTest, PackageCachePolicy) {
  static constexpr char kFile[] = R"F(
  fuchsia-pkg://fuchsia.com/pkgctl#meta/pkgctl.cmx
  )F";

  // Stub out a dispatcher.  We won't actually run anything on it, but some
  // things in PolicyChecker assert they can grab the implicit default eventloop, so
  // keep them happy.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));

  // Add the allowlist.
  ASSERT_TRUE(files::CreateDirectoryAt(dirfd.get(), "allowlist"));
  auto filename = NewFile(dir, "allowlist/package_cache.txt", kFile);

  FuchsiaPkgUrl fp;
  PolicyChecker policy_checker(std::move(dirfd));

  // "Vanilla" package url, without variant or hash
  fp.Parse("fuchsia-pkg://fuchsia.com/pkgctl#meta/pkgctl.cmx");
  EXPECT_TRUE(policy_checker.CheckPackageCache(fp));

  // Variants and hashes should be thrown away
  fp.Parse("fuchsia-pkg://fuchsia.com/pkgctl/0?hash=123#meta/pkgctl.cmx");
  EXPECT_TRUE(policy_checker.CheckPackageCache(fp));

  // Check exclusion
  fp.Parse("fuchsia-pkg://fuchsia.com/stash#meta/stash.cmx");
  EXPECT_FALSE(policy_checker.CheckPackageCache(fp));
}

TEST_F(PolicyCheckerTest, PkgFsVersionsPolicy) {
  static constexpr char kFile[] = R"F(
  fuchsia-pkg://fuchsia.com/pkg-cache#meta/pkg-cache.cmx
  )F";

  // Stub out a dispatcher.  We won't actually run anything on it, but some
  // things in PolicyChecker assert they can grab the implicit default eventloop, so
  // keep them happy.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));

  // Add the allowlist.
  ASSERT_TRUE(files::CreateDirectoryAt(dirfd.get(), "allowlist"));
  auto filename = NewFile(dir, "allowlist/pkgfs_versions.txt", kFile);

  FuchsiaPkgUrl fp;
  PolicyChecker policy_checker(std::move(dirfd));

  // "Vanilla" package url, without variant or hash
  fp.Parse("fuchsia-pkg://fuchsia.com/pkg-cache#meta/pkg-cache.cmx");
  EXPECT_TRUE(policy_checker.CheckPkgFsVersions(fp));

  // Variants and hashes should be thrown away
  fp.Parse("fuchsia-pkg://fuchsia.com/pkg-cache/0?hash=123#meta/pkg-cache.cmx");
  EXPECT_TRUE(policy_checker.CheckPkgFsVersions(fp));

  // Check exclusion
  fp.Parse("fuchsia-pkg://fuchsia.com/stash#meta/stash.cmx");
  EXPECT_FALSE(policy_checker.CheckPkgFsVersions(fp));
}

TEST_F(PolicyCheckerTest, RootJobPolicy) {
  static constexpr char kFile[] = R"F(
  fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx
  )F";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));

  // Add the allowlist.
  ASSERT_TRUE(files::CreateDirectoryAt(dirfd.get(), "allowlist"));
  auto filename = NewFile(dir, "allowlist/root_job.txt", kFile);

  FuchsiaPkgUrl fp;
  PolicyChecker policy_checker(std::move(dirfd));

  // "Vanilla" package url, without variant or hash
  fp.Parse("fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx");
  EXPECT_TRUE(policy_checker.CheckRootJob(fp));

  // Variants and hashes should be thrown away
  fp.Parse("fuchsia-pkg://fuchsia.com/foo/0?hash=123#meta/foo.cmx");
  EXPECT_TRUE(policy_checker.CheckRootJob(fp));

  // Check exclusion
  fp.Parse("fuchsia-pkg://fuchsia.com/bar#meta/bar.cmx");
  EXPECT_FALSE(policy_checker.CheckRootJob(fp));
}

TEST_F(PolicyCheckerTest, RootResourcePolicy) {
  static constexpr char kFile[] = R"F(
  fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx
  )F";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));

  // Add the allowlist.
  ASSERT_TRUE(files::CreateDirectoryAt(dirfd.get(), "allowlist"));
  auto filename = NewFile(dir, "allowlist/root_resource.txt", kFile);

  FuchsiaPkgUrl fp;
  PolicyChecker policy_checker(std::move(dirfd));

  // "Vanilla" package url, without variant or hash
  fp.Parse("fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx");
  EXPECT_TRUE(policy_checker.CheckRootResource(fp));

  // Variants and hashes should be thrown away
  fp.Parse("fuchsia-pkg://fuchsia.com/foo/0?hash=123#meta/foo.cmx");
  EXPECT_TRUE(policy_checker.CheckRootResource(fp));

  // Check exclusion
  fp.Parse("fuchsia-pkg://fuchsia.com/bar#meta/bar.cmx");
  EXPECT_FALSE(policy_checker.CheckRootResource(fp));
}

TEST_F(PolicyCheckerTest, SystemUpdaterPolicy) {
  static constexpr char kFile[] = R"F(
  fuchsia-pkg://fuchsia.com/system-update-checker#meta/system-update-checker.cmx
  )F";

  // Stub out a dispatcher.  We won't actually run anything on it, but some
  // things in PolicyChecker assert they can grab the implicit default eventloop, so
  // keep them happy.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));

  // Add the allowlist.
  ASSERT_TRUE(files::CreateDirectoryAt(dirfd.get(), "allowlist"));
  auto filename = NewFile(dir, "allowlist/system_updater.txt", kFile);

  FuchsiaPkgUrl fp;
  PolicyChecker policy_checker(std::move(dirfd));

  // "Vanilla" package url, without variant or hash
  fp.Parse("fuchsia-pkg://fuchsia.com/system-update-checker#meta/system-update-checker.cmx");
  EXPECT_TRUE(policy_checker.CheckSystemUpdater(fp));

  // Variants and hashes should be thrown away
  fp.Parse("fuchsia-pkg://fuchsia.com/system-update-checker/0?hash=123#meta/system-update-checker.cmx");
  EXPECT_TRUE(policy_checker.CheckSystemUpdater(fp));

  // Check exclusion
  fp.Parse("fuchsia-pkg://fuchsia.com/foo#meta/bar.cmx");
  EXPECT_FALSE(policy_checker.CheckSystemUpdater(fp));
}

TEST_F(PolicyCheckerTest, VmexResourcePolicy) {
  static constexpr char kFile[] = R"F(
  fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx
  )F";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));

  // Add the allowlist.
  ASSERT_TRUE(files::CreateDirectoryAt(dirfd.get(), "allowlist"));
  auto filename = NewFile(dir, "allowlist/vmex_resource.txt", kFile);

  FuchsiaPkgUrl fp;
  PolicyChecker policy_checker(std::move(dirfd));

  // "Vanilla" package url, without variant or hash
  fp.Parse("fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx");
  EXPECT_TRUE(policy_checker.CheckVmexResource(fp));

  // Variants and hashes should be thrown away
  fp.Parse("fuchsia-pkg://fuchsia.com/foo/0?hash=123#meta/foo.cmx");
  EXPECT_TRUE(policy_checker.CheckVmexResource(fp));

  // Check exclusion
  fp.Parse("fuchsia-pkg://fuchsia.com/bar#meta/bar.cmx");
  EXPECT_FALSE(policy_checker.CheckVmexResource(fp));
}

}  // namespace component
