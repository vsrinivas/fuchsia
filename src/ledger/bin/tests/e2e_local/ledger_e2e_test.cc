// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <string.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/rng/system_random.h"
#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/app/serialization_version.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/platform/detached_path.h"
#include "src/ledger/bin/platform/fd.h"
#include "src/ledger/bin/platform/platform.h"
#include "src/ledger/bin/platform/scoped_tmp_location.h"
#include "src/ledger/bin/public/status.h"
#include "src/ledger/bin/testing/ledger_matcher.h"
#include "src/ledger/cloud_provider_in_memory/lib/fake_cloud_provider.h"
#include "src/ledger/cloud_provider_in_memory/lib/types.h"
#include "src/ledger/lib/callback/capture.h"
#include "src/ledger/lib/callback/set_when_called.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/loop_fixture/real_loop_fixture.h"
#include "src/ledger/lib/vmo/strings.h"
#include "third_party/abseil-cpp/absl/strings/escaping.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace test {
namespace e2e_local {
namespace {

using ::testing::ElementsAre;

// Recursively searches for a directory with name |target_dir| and returns
// whether it was found. If found, |path_to_dir| is updated with the path from
// the source path.
bool FindPathToDir(ledger::FileSystem* file_system, const ledger::DetachedPath& root_path,
                   absl::string_view target_dir, ledger::DetachedPath* path_to_dir) {
  std::vector<std::string> directory_entries;
  if (!file_system->GetDirectoryContents(root_path, &directory_entries)) {
    LEDGER_LOG(ERROR) << "Error while reading directory contents at: " << root_path.path();
    return false;
  }
  for (const std::string& entry : directory_entries) {
    ledger::DetachedPath current_path = root_path.SubPath(entry);
    if (file_system->IsDirectory(
            ledger::DetachedPath(current_path.root_fd(), current_path.path()))) {
      if (entry == target_dir) {
        *path_to_dir = std::move(current_path);
        return true;
      }
      if (FindPathToDir(file_system, current_path, target_dir, path_to_dir)) {
        // The page path was found in |current_path|.
        return true;
      }
    }
  }
  return false;
}

template <class A>
bool Equals(const fidl::VectorPtr<uint8_t>& a1, const A& a2) {
  if (a1->size() != a2.size())
    return false;
  return memcmp(a1->data(), a2.data(), a1->size()) == 0;
}

std::vector<uint8_t> TestArray() {
  std::string value = "value";
  std::vector<uint8_t> result(value.size());
  memcpy(&result.at(0), &value[0], value.size());
  return result;
}

class LedgerEndToEndTest : public ledger::RealLoopFixture {
 public:
  LedgerEndToEndTest() : component_context_(sys::ComponentContext::Create()) {
    component_context()->svc()->Connect(launcher_.NewRequest());
  }
  ~LedgerEndToEndTest() override = default;

 protected:
  void Init(std::vector<std::string> additional_args) {
    platform_ = ledger::MakePlatform();

    fidl::InterfaceHandle<fuchsia::io::Directory> child_directory;
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = "fuchsia-pkg://fuchsia.com/ledger#meta/ledger.cmx";
    launch_info.directory_request = child_directory.NewRequest().TakeChannel();
    launch_info.arguments = std::vector<std::string>{};
    ledger::AppendGarbageCollectionPolicyFlags(ledger::kTestingGarbageCollectionPolicy,
                                               &launch_info);
    for (auto& additional_arg : additional_args) {
      launch_info.arguments->push_back(additional_arg);
    }
    launcher_->CreateComponent(std::move(launch_info), ledger_controller_.NewRequest());

    ledger_controller_.set_error_handler([this](zx_status_t status) {
      for (const auto& callback : ledger_shutdown_callbacks_) {
        callback();
      }
    });

    ledger_repository_factory_.set_error_handler([](zx_status_t status) {
      if (status != ZX_ERR_PEER_CLOSED) {
        ADD_FAILURE() << "Ledger repository error: " << status;
      }
    });
    sys::ServiceDirectory child_services(std::move(child_directory));
    child_services.Connect(ledger_repository_factory_.NewRequest());
    child_services.Connect(controller_.NewRequest());
  }

  void RegisterShutdownCallback(fit::function<void()> callback) {
    ledger_shutdown_callbacks_.push_back(std::move(callback));
  }

  sys::ComponentContext* component_context() { return component_context_.get(); }

  rng::Random* random() { return &random_; }

 private:
  fuchsia::sys::ComponentControllerPtr ledger_controller_;
  std::vector<fit::function<void()>> ledger_shutdown_callbacks_;
  std::unique_ptr<sys::ComponentContext> component_context_;
  fuchsia::sys::LauncherPtr launcher_;
  rng::SystemRandom random_;

 protected:
  std::unique_ptr<ledger::Platform> platform_;
  ledger_internal::LedgerRepositoryFactoryPtr ledger_repository_factory_;
  fidl::SynchronousInterfacePtr<ledger::Ledger> ledger_;
  fidl::SynchronousInterfacePtr<ledger_internal::LedgerController> controller_;
};

TEST_F(LedgerEndToEndTest, PutAndGet) {
  Init({});
  ledger_internal::LedgerRepositoryPtr ledger_repository;
  std::unique_ptr<ledger::ScopedTmpLocation> tmp_location =
      platform_->file_system()->CreateScopedTmpLocation();
  ledger_repository_factory_->GetRepository(
      ledger::CloneChannelFromFileDescriptor(tmp_location->path().root_fd()), nullptr, "",
      ledger_repository.NewRequest());

  ledger_repository->GetLedger(TestArray(), ledger_.NewRequest());
  ledger_repository->Sync(ledger::Capture(QuitLoopClosure()));
  RunLoop();

  fidl::SynchronousInterfacePtr<ledger::Page> page;
  ledger_->GetRootPage(page.NewRequest());
  page->Put(TestArray(), TestArray());
  fidl::SynchronousInterfacePtr<ledger::PageSnapshot> snapshot;
  page->GetSnapshot(snapshot.NewRequest(), {}, nullptr);
  fuchsia::ledger::PageSnapshot_Get_Result result;
  EXPECT_EQ(snapshot->Get(TestArray(), &result), ZX_OK);
  EXPECT_THAT(result, ledger::MatchesString(convert::ToString(TestArray())));

  snapshot.Unbind();
  page.Unbind();
  ledger_.Unbind();
  ledger_repository->Close();
  ledger_repository.set_error_handler([this](zx_status_t) { QuitLoop(); });
  RunLoop();
}

TEST_F(LedgerEndToEndTest, Terminate) {
  Init({});
  bool called = false;
  RegisterShutdownCallback([this, &called] {
    called = true;
    QuitLoop();
  });
  controller_->Terminate();
  RunLoop();
  EXPECT_TRUE(called);
}

TEST_F(LedgerEndToEndTest, ClearPage) {
  Init({});
  ledger_internal::LedgerRepositoryPtr ledger_repository;
  std::unique_ptr<ledger::ScopedTmpLocation> tmp_location =
      platform_->file_system()->CreateScopedTmpLocation();
  ledger_repository_factory_->GetRepository(
      ledger::CloneChannelFromFileDescriptor(tmp_location->path().root_fd()), nullptr, "",
      ledger_repository.NewRequest());

  ledger_repository->GetLedger(TestArray(), ledger_.NewRequest());
  ledger_repository->Sync(ledger::Capture(QuitLoopClosure()));
  RunLoop();

  int page_count = 5;

  std::vector<ledger::DetachedPath> page_paths;
  page_paths.reserve(page_count);

  // Create 5 pages, add contents and clear them.
  for (int i = 0; i < page_count; ++i) {
    fidl::SynchronousInterfacePtr<ledger::Page> page;
    ledger_->GetPage(nullptr, page.NewRequest());
    ASSERT_EQ(ZX_OK, ledger_->Sync());

    // Check that the directory has been created.
    ledger::PageId page_id;
    page->GetId(&page_id);

    // The following is assuming that the page's folder is using the name
    // <base64(page_id)>.
    std::string page_dir_name = absl::WebSafeBase64Escape(convert::ExtendedStringView(page_id.id));
    ledger::DetachedPath page_path;
    ASSERT_TRUE(
        FindPathToDir(platform_->file_system(), tmp_location->path(), page_dir_name, &page_path))
        << "Failed to find page's directory. Expected to find directory named "
           "`base64(page_id)`: "
        << page_dir_name;
    page_paths.push_back(std::move(page_path));

    // Insert an entry.
    page->Put(TestArray(), TestArray());

    // Clear the page and close it.
    page->Clear();
    page.Unbind();
  }

  // Make sure all directories have been deleted.
  for (const ledger::DetachedPath& path : page_paths) {
    RunLoopUntil([&] { return !platform_->file_system()->IsDirectory(path); });
    EXPECT_FALSE(platform_->file_system()->IsDirectory(
        ledger::DetachedPath(tmp_location->path().root_fd(), path.path())));
  }

  ledger_.Unbind();
  ledger_repository->Close();
  ledger_repository.set_error_handler([this](zx_status_t) { QuitLoop(); });
  RunLoop();
}

// Verifies the cloud erase recovery in case of a cloud that was erased before
// startup.
//
// Expected behavior: Ledger disconnects the clients and the local state is
// cleared.
TEST_F(LedgerEndToEndTest, CloudEraseRecoveryOnInitialCheck) {
  Init({});
  bool ledger_shut_down = false;
  RegisterShutdownCallback([&ledger_shut_down] { ledger_shut_down = true; });

  std::unique_ptr<ledger::ScopedTmpLocation> tmp_location =
      platform_->file_system()->CreateScopedTmpLocation();
  const ledger::DetachedPath content_path(tmp_location->path().root_fd(),
                                          convert::ToString(ledger::kSerializationVersion));
  const ledger::DetachedPath deletion_sentinel_path = content_path.SubPath("sentinel");
  ASSERT_TRUE(platform_->file_system()->CreateDirectory(content_path));
  ASSERT_TRUE(platform_->file_system()->WriteFile(deletion_sentinel_path, ""));
  ASSERT_TRUE(platform_->file_system()->IsFile(deletion_sentinel_path));

  // Create a cloud provider configured to trigger the cloude erase recovery on
  // initial check.
  bool device_set_watcher_set;
  auto cloud_provider =
      std::move(ledger::FakeCloudProvider::Builder(dispatcher(), random())
                    .SetCloudEraseOnCheck(ledger::CloudEraseOnCheck::YES)
                    .SetOnWatcherSet(ledger::SetWhenCalled(&device_set_watcher_set)))
          .Build();
  {
    cloud_provider::CloudProviderPtr cloud_provider_ptr;
    fidl::Binding<cloud_provider::CloudProvider> cloud_provider_binding(
        cloud_provider.get(), cloud_provider_ptr.NewRequest());
    ledger_internal::LedgerRepositoryPtr ledger_repository;
    ledger_repository_factory_->GetRepository(
        ledger::CloneChannelFromFileDescriptor(tmp_location->path().root_fd()),
        std::move(cloud_provider_ptr), "user_id", ledger_repository.NewRequest());

    RunLoopUntil([&] { return device_set_watcher_set; });

    bool repo_disconnected = false;
    ledger_repository.set_error_handler(
        [&repo_disconnected](zx_status_t /*status*/) { repo_disconnected = true; });

    ledger_repository->Close();
    RunLoopUntil([&] { return repo_disconnected; });
  }

  // The device fingerprint is set. Now we can test its erasure.
  cloud_provider::CloudProviderPtr cloud_provider_ptr;
  fidl::Binding<cloud_provider::CloudProvider> cloud_provider_binding(
      cloud_provider.get(), cloud_provider_ptr.NewRequest());
  ledger_internal::LedgerRepositoryPtr ledger_repository;
  ledger_repository_factory_->GetRepository(
      ledger::CloneChannelFromFileDescriptor(tmp_location->path().root_fd()),
      std::move(cloud_provider_ptr), "user_id", ledger_repository.NewRequest());

  bool repo_disconnected = false;
  ledger_repository.set_error_handler(
      [&repo_disconnected](zx_status_t /*status*/) { repo_disconnected = true; });

  // Run the message loop until Ledger clears the repo directory and disconnects
  // the client.
  RunLoopUntil([&] {
    return !platform_->file_system()->IsFile(deletion_sentinel_path) && repo_disconnected;
  });
  EXPECT_FALSE(platform_->file_system()->IsFile(deletion_sentinel_path));
  EXPECT_TRUE(repo_disconnected);

  // Make sure all the contents are deleted. Only the staging directory should be present.
  std::vector<std::string> directory_entries;
  EXPECT_TRUE(
      platform_->file_system()->GetDirectoryContents(tmp_location->path(), &directory_entries));
  EXPECT_THAT(directory_entries, ElementsAre("staging"));

  // Verify that the Ledger app didn't crash.
  EXPECT_FALSE(ledger_shut_down);
}

// Verifies the cloud erase recovery in case of a cloud that is erased while
// Ledger is connected to it.
//
// Expected behavior: Ledger disconnects the clients and the local state is
// cleared.
TEST_F(LedgerEndToEndTest, CloudEraseRecoveryFromTheWatcher) {
  Init({});
  bool ledger_shut_down = false;
  RegisterShutdownCallback([&ledger_shut_down] { ledger_shut_down = true; });

  ledger_internal::LedgerRepositoryPtr ledger_repository;
  std::unique_ptr<ledger::ScopedTmpLocation> tmp_location =
      platform_->file_system()->CreateScopedTmpLocation();
  ledger::DetachedPath tmp_location_path = tmp_location->path();
  ledger::DetachedPath content_path =
      tmp_location_path.SubPath(convert::ToString(ledger::kSerializationVersion));
  ledger::DetachedPath deletion_path = content_path.SubPath("sentinel");
  ASSERT_TRUE(platform_->file_system()->CreateDirectory(content_path));
  ASSERT_TRUE(platform_->file_system()->WriteFile(deletion_path, ""));
  ASSERT_TRUE(platform_->file_system()->IsFile(deletion_path));

  // Create a cloud provider configured to trigger the cloud erase recovery
  // while Ledger is connected.
  auto cloud_provider = std::move(ledger::FakeCloudProvider::Builder(dispatcher(), random())
                                      .SetCloudEraseFromWatcher(ledger::CloudEraseFromWatcher::YES))
                            .Build();
  cloud_provider::CloudProviderPtr cloud_provider_ptr;
  fidl::Binding<cloud_provider::CloudProvider> cloud_provider_binding(
      cloud_provider.get(), cloud_provider_ptr.NewRequest());

  ledger_repository_factory_->GetRepository(
      ledger::CloneChannelFromFileDescriptor(tmp_location_path.root_fd()),
      std::move(cloud_provider_ptr), "user_id", ledger_repository.NewRequest());

  bool repo_disconnected = false;
  ledger_repository.set_error_handler(
      [&repo_disconnected](zx_status_t status) { repo_disconnected = true; });

  // Run the message loop until Ledger clears the repo directory and disconnects
  // the client.
  RunLoopUntil(
      [&] { return !platform_->file_system()->IsFile(deletion_path) && repo_disconnected; });
  EXPECT_FALSE(platform_->file_system()->IsFile(deletion_path));
  EXPECT_TRUE(repo_disconnected);

  // Verify that the Ledger app didn't crash.
  EXPECT_FALSE(ledger_shut_down);
}

// Verifies that Ledger instance continues to work even if the cloud provider
// goes away (for example, because it crashes).
//
// In the future, we need to also be able to reconnect/request a new cloud
// provider, see LE-567.
TEST_F(LedgerEndToEndTest, HandleCloudProviderDisconnectBeforePageInit) {
  Init({});
  bool ledger_app_shut_down = false;
  RegisterShutdownCallback([&ledger_app_shut_down] { ledger_app_shut_down = true; });
  std::unique_ptr<ledger::ScopedTmpLocation> tmp_location =
      platform_->file_system()->CreateScopedTmpLocation();

  cloud_provider::CloudProviderPtr cloud_provider_ptr;
  ledger_internal::LedgerRepositoryPtr ledger_repository;
  ledger::FakeCloudProvider cloud_provider(dispatcher(), random());
  fidl::Binding<cloud_provider::CloudProvider> cloud_provider_binding(
      &cloud_provider, cloud_provider_ptr.NewRequest());
  ledger_repository_factory_->GetRepository(
      ledger::CloneChannelFromFileDescriptor(tmp_location->path().root_fd()),
      std::move(cloud_provider_ptr), "user_id", ledger_repository.NewRequest());

  ledger_repository->GetLedger(TestArray(), ledger_.NewRequest());
  ledger_repository->Sync(ledger::Capture(QuitLoopClosure()));
  RunLoop();

  // Close the cloud provider channel.
  cloud_provider_binding.Unbind();

  // Write and read some data to verify that Ledger still works.
  fidl::SynchronousInterfacePtr<ledger::Page> page;
  ledger_->GetPage(nullptr, page.NewRequest());
  page->Put(TestArray(), TestArray());
  fidl::SynchronousInterfacePtr<ledger::PageSnapshot> snapshot;
  page->GetSnapshot(snapshot.NewRequest(), {}, nullptr);
  fuchsia::mem::BufferPtr value;
  fuchsia::ledger::PageSnapshot_Get_Result result;
  EXPECT_EQ(snapshot->Get(TestArray(), &result), ZX_OK);
  EXPECT_THAT(result, ledger::MatchesString(convert::ToString(TestArray())));

  // Verify that the Ledger app didn't crash or shut down.
  EXPECT_TRUE(ledger_repository);
  EXPECT_FALSE(ledger_app_shut_down);

  snapshot.Unbind();
  page.Unbind();
  ledger_.Unbind();
  ledger_repository->Close();
  ledger_repository.set_error_handler([this](zx_status_t) { QuitLoop(); });
  RunLoop();
}

TEST_F(LedgerEndToEndTest, HandleCloudProviderDisconnectBetweenReadAndWrite) {
  Init({});
  bool ledger_app_shut_down = false;
  RegisterShutdownCallback([&ledger_app_shut_down] { ledger_app_shut_down = true; });
  ledger::Status status;
  std::unique_ptr<ledger::ScopedTmpLocation> tmp_location =
      platform_->file_system()->CreateScopedTmpLocation();

  cloud_provider::CloudProviderPtr cloud_provider_ptr;
  ledger_internal::LedgerRepositoryPtr ledger_repository;
  ledger::FakeCloudProvider cloud_provider(dispatcher(), random());
  fidl::Binding<cloud_provider::CloudProvider> cloud_provider_binding(
      &cloud_provider, cloud_provider_ptr.NewRequest());
  ledger_repository_factory_->GetRepository(
      ledger::CloneChannelFromFileDescriptor(tmp_location->path().root_fd()),
      std::move(cloud_provider_ptr), "user_id", ledger_repository.NewRequest());

  ledger_repository->GetLedger(TestArray(), ledger_.NewRequest());
  ledger_repository->Sync(ledger::Capture(QuitLoopClosure()));
  RunLoop();

  // Write some data.
  fidl::SynchronousInterfacePtr<ledger::Page> page;
  ledger_->GetPage(nullptr, page.NewRequest());
  status = ledger::Status::INTERNAL_ERROR;
  page->Put(TestArray(), TestArray());

  // Close the cloud provider channel.
  cloud_provider_binding.Unbind();

  // Read the data back.
  fidl::SynchronousInterfacePtr<ledger::PageSnapshot> snapshot;
  status = ledger::Status::INTERNAL_ERROR;
  page->GetSnapshot(snapshot.NewRequest(), {}, nullptr);
  fuchsia::mem::BufferPtr value;
  fuchsia::ledger::PageSnapshot_Get_Result result;
  EXPECT_EQ(snapshot->Get(TestArray(), &result), ZX_OK);
  EXPECT_THAT(result, ledger::MatchesString(convert::ToString(TestArray())));

  // Verify that the Ledger app didn't crash or shut down.
  EXPECT_TRUE(ledger_repository);
  EXPECT_FALSE(ledger_app_shut_down);

  snapshot.Unbind();
  page.Unbind();
  ledger_.Unbind();
  ledger_repository->Close();
  ledger_repository.set_error_handler([this](zx_status_t) { QuitLoop(); });
  RunLoop();
}

}  // namespace
}  // namespace e2e_local
}  // namespace test
