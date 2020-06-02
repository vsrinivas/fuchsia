// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/sys/appmgr/component_id_index.h"
#include "src/sys/appmgr/realm.h"

namespace component {
namespace {

constexpr char kIndexFilePath[] = "component_id_index";
const char kEmptyComponentIdIndex[] = R"({ "instances": [] })";
const char kExampleInstanceId[] =
    "8c90d44863ff67586cf6961081feba4f760decab8bbbee376a3bfbc77b351280";

class StorageTest : public ::gtest::RealLoopFixture {
 protected:
  StorageTest() { ZX_ASSERT(tmp_dir_.NewTempDir(&root_storage_dir_)); }

  // Creates a root realm with label = componnet::internal::kRootLabel ("app").
  std::unique_ptr<Realm> CreateRootRealm(const std::string& root_storage_path,
                                         fbl::unique_fd appmgr_config_dir) {
    auto environment_services = sys::ServiceDirectory::CreateFromNamespace();
    fuchsia::sys::ServiceListPtr root_realm_services(new fuchsia::sys::ServiceList);
    files::CreateDirectoryAt(appmgr_config_dir.get(), "scheme_map");
    auto component_id_index =
        ComponentIdIndex::CreateFromAppmgrConfigDir(appmgr_config_dir).take_value();
    fuchsia::sys::EnvironmentOptions opts;
    opts.delete_storage_on_death = false;
    RealmArgs realm_args = RealmArgs::MakeWithAdditionalServices(
        nullptr, internal::kRootLabel, files::JoinPath(root_storage_dir_, "data"),
        files::JoinPath(root_storage_dir_, "data/cache"), files::JoinPath(root_storage_dir_, "tmp"),
        std::move(environment_services), false, std::move(root_realm_services), std::move(opts),
        std::move(appmgr_config_dir), std::move(component_id_index));
    return Realm::Create(std::move(realm_args));
  }

  Realm* CreateChildRealm(
      Realm* parent, const std::string& label,
      fidl::InterfaceRequest<fuchsia::sys::EnvironmentController> env_ctrl_req) {
    fuchsia::sys::EnvironmentOptions opts;
    opts.delete_storage_on_death = false;

    fuchsia::sys::EnvironmentPtr env;
    parent->CreateNestedEnvironment(env.NewRequest(), std::move(env_ctrl_req), "child_realm",
                                    nullptr, std::move(opts));
    for (const auto& item : parent->children()) {
      if (item.first->label() == label) {
        return item.first;
      }
    }
    return nullptr;
  }

  std::string root_storage_dir() { return root_storage_dir_; }

  fbl::unique_fd MakeAppmgrConfigDirWithIndex(std::string json_index) {
    fbl::unique_fd ufd(open(tmp_dir_.path().c_str(), O_RDONLY));
    ZX_ASSERT(ufd.is_valid());
    ZX_ASSERT(files::WriteFileAt(ufd.get(), kIndexFilePath, json_index.data(), json_index.size()));
    return ufd;
  }

 private:
  files::ScopedTempDir tmp_dir_;
  std::string root_storage_dir_;
};

// Test the storage directory path for a component when it doesn't have a component ID
// index.
TEST_F(StorageTest, DirPathWithoutInstanceId) {
  auto root_realm =
      CreateRootRealm(root_storage_dir(), MakeAppmgrConfigDirWithIndex(kEmptyComponentIdIndex));

  fuchsia::sys::EnvironmentControllerPtr child_env_ctrl;
  auto child_realm = CreateChildRealm(root_realm.get(), "child_realm", child_env_ctrl.NewRequest());

  FuchsiaPkgUrl url;
  ZX_ASSERT(url.Parse("fuchsia-pkg://fuchsia.com/my_pkg#meta/my_component.cmx"));

  EXPECT_EQ(child_realm->IsolatedPathForComponentInstance(url, internal::StorageType::DATA),
            files::JoinPath(root_storage_dir(),
                            "data/r/child_realm/fuchsia.com:my_pkg:0#meta:my_component.cmx"));

  // Ensure that the moniker-based directory is created.
  EXPECT_TRUE(files::IsDirectory(files::JoinPath(
      root_storage_dir(), "data/r/child_realm/fuchsia.com:my_pkg:0#meta:my_component.cmx")));
}

// Test the storage directory path for a component when it has a component ID.
TEST_F(StorageTest, DirPathWithInstanceId) {
  auto root_realm = CreateRootRealm(
      root_storage_dir(), MakeAppmgrConfigDirWithIndex(fxl::Substitute(R"(
        {
          "instances": [
            {
              "instance_id": "$0",
              "appmgr_moniker": {
                "realm_path": ["app", "child_realm"],
                "url": "fuchsia-pkg://fuchsia.com/my_pkg#meta/my_component.cmx"
              }
            }
          ]
        }
      )",
                                                                       kExampleInstanceId)));

  fuchsia::sys::EnvironmentControllerPtr child_env_ctrl;
  auto child_realm = CreateChildRealm(root_realm.get(), "child_realm", child_env_ctrl.NewRequest());

  FuchsiaPkgUrl url;
  ZX_ASSERT(url.Parse("fuchsia-pkg://fuchsia.com/my_pkg#meta/my_component.cmx"));

  auto actual_storage_path =
      child_realm->IsolatedPathForComponentInstance(url, internal::StorageType::DATA);
  EXPECT_EQ(actual_storage_path,
            files::JoinPath(root_storage_dir(),
                            fxl::Substitute("data/persistent/$0", kExampleInstanceId)));

  // Ensure that the instance ID based directory is created.
  EXPECT_TRUE(files::IsDirectory(files::JoinPath(
      root_storage_dir(), fxl::Substitute("data/persistent/$0", kExampleInstanceId))));

  // Ensure that the moniker based directory does not exist.
  EXPECT_FALSE(files::IsDirectory(files::JoinPath(
      root_storage_dir(), "data/r/child_realm/fuchsia.com:my_pkg:0#meta:my_component.cmx")));
}

// Test that when a component's storage directory is moved once it is assigned an instance ID.
TEST_F(StorageTest, MoveDirToInstanceId) {
  // Step 1: ensure storage directory exists for component without an instance ID.
  {
    auto root_realm =
        CreateRootRealm(root_storage_dir(), MakeAppmgrConfigDirWithIndex(kEmptyComponentIdIndex));

    fuchsia::sys::EnvironmentControllerPtr child_env_ctrl;
    auto child_realm =
        CreateChildRealm(root_realm.get(), "child_realm", child_env_ctrl.NewRequest());

    FuchsiaPkgUrl url;
    ZX_ASSERT(url.Parse("fuchsia-pkg://fuchsia.com/my_pkg#meta/my_component.cmx"));

    EXPECT_EQ(child_realm->IsolatedPathForComponentInstance(url, internal::StorageType::DATA),
              files::JoinPath(root_storage_dir(),
                              "data/r/child_realm/fuchsia.com:my_pkg:0#meta:my_component.cmx"));
  }

  // Ensure that the moniker based directory is created.
  EXPECT_TRUE(files::IsDirectory(files::JoinPath(
      root_storage_dir(), "data/r/child_realm/fuchsia.com:my_pkg:0#meta:my_component.cmx")));

  // Step 2: Spin up the root realm again, this time assigning the component with an instance ID.
  {
    auto root_realm = CreateRootRealm(
        root_storage_dir(), MakeAppmgrConfigDirWithIndex(fxl::Substitute(R"(
        {
          "instances": [
            {
              "instance_id": "$0",
              "appmgr_moniker": {
                "realm_path": ["app", "child_realm"],
                "url": "fuchsia-pkg://fuchsia.com/my_pkg#meta/my_component.cmx"
              }
            }
          ]
        }
      )",
                                                                         kExampleInstanceId)));

    fuchsia::sys::EnvironmentControllerPtr child_env_ctrl;
    auto child_realm =
        CreateChildRealm(root_realm.get(), "child_realm", child_env_ctrl.NewRequest());

    FuchsiaPkgUrl url;
    ZX_ASSERT(url.Parse("fuchsia-pkg://fuchsia.com/my_pkg#meta/my_component.cmx"));

    auto actual_storage_path =
        child_realm->IsolatedPathForComponentInstance(url, internal::StorageType::DATA);
    EXPECT_EQ(actual_storage_path,
              files::JoinPath(root_storage_dir(),
                              fxl::Substitute("data/persistent/$0", kExampleInstanceId)));
  }

  // Ensure that the moniker based directory has been moved to the instance ID based directory.
  EXPECT_FALSE(files::IsDirectory(files::JoinPath(
      root_storage_dir(), "data/r/child_realm/fuchsia.com:my_pkg:0#meta:my_component.cmx")));
  EXPECT_TRUE(files::IsDirectory(files::JoinPath(
      root_storage_dir(), fxl::Substitute("data/persistent/$0", kExampleInstanceId))));
}

}  // namespace
}  // namespace component
