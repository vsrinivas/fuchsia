// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_POLICY_CHECKER_H_
#define SRC_SYS_APPMGR_POLICY_CHECKER_H_

#include <optional>
#include <string>

#include "gtest/gtest_prod.h"
#include "src/lib/cmx/sandbox.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/pkg_url/fuchsia_pkg_url.h"

namespace component {

// Holds the list of policies that are returned by the policy checker. These are
// used by the Realm to correctly setup the environment.
struct SecurityPolicy {
  bool enable_ambient_executable = false;
  bool enable_component_event_provider = false;
};

// The job of the `PolicyChecker` is to enforce that security policies placed
// on the sandbox are enforced at runtime. For example if a component attempts
// to enable ambient executability within its component manifiest but is not on
// a specific allowlist defined in `//src/security/policy` this object will
// catch it.
class PolicyChecker final {
 public:
  explicit PolicyChecker(fxl::UniqueFD config);
  // Returns a Policy object if the check was successful else no policy could
  // be set due to a policy being violated. If nullopt is returned the
  // component should not be launched.
  std::optional<SecurityPolicy> Check(const SandboxMetadata& sandbox, const FuchsiaPkgUrl& fp);

 private:
  fxl::UniqueFD config_;

  bool CheckDeprecatedShell(std::string ns_id);
  bool CheckDeprecatedAmbientReplaceAsExecutable(std::string ns_id);
  bool CheckComponentEventProvider(std::string ns_id);
  bool CheckPackageResolver(std::string ns_id);
  bool CheckPackageCache(std::string ns_id);
  bool CheckPkgFsVersions(std::string ns_id);

  FRIEND_TEST(PolicyCheckerTest, ReplaceAsExecPolicyPresent);
  FRIEND_TEST(PolicyCheckerTest, ReplaceAsExecPolicyAbsent);
  FRIEND_TEST(PolicyCheckerTest, PackageResolverPolicy);
  FRIEND_TEST(PolicyCheckerTest, PackageCachePolicy);
  FRIEND_TEST(PolicyCheckerTest, PkgFsVersionsPolicy);
};

}  // end of namespace component.

#endif  // SRC_SYS_APPMGR_POLICY_CHECKER_H_
