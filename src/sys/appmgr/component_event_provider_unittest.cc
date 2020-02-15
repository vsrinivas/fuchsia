// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/sys/internal/cpp/fidl.h>
#include <fuchsia/sys/internal/cpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/testing/fake_component.h>
#include <unistd.h>

#include "gtest/gtest.h"
#include "lib/sys/cpp/testing/component_context_provider.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/sys/appmgr/component_controller_impl.h"
#include "src/sys/appmgr/realm.h"

namespace component {
namespace {

// Listener is not discovearble, and needs an explicit name.
const char kListenerName[] = "fuchsia::sys::internal::ComponentEventListener";

class FakeListener : public fuchsia::sys::internal::testing::ComponentEventListener_TestBase {
 public:
  FakeListener() { component_.AddPublicService(bindings_.GetHandler(this), kListenerName); }

  void NotImplemented_(const std::string& name) override { FAIL() << "not implemented: " << name; }

 private:
  sys::testing::FakeComponent component_;
  fidl::BindingSet<fuchsia::sys::internal::ComponentEventListener> bindings_;
};

class ComponentEventProviderTest : public ::gtest::TestLoopFixture {
 protected:
  void SetUp() {
    TestLoopFixture::SetUp();
    fake_listener_service_ = std::make_unique<FakeListener>();
    context_provider_ = std::make_unique<sys::testing::ComponentContextProvider>(dispatcher());
  }

  // Borrowed from realm_unittest.cc, consider deduping if things get overly copied around.
  std::unique_ptr<Realm> CreateTestRealm(fxl::UniqueFD dirfd) {
    files::CreateDirectoryAt(dirfd.get(), "scheme_map");
    auto environment_services = sys::ServiceDirectory::CreateFromNamespace();
    fuchsia::sys::ServiceListPtr root_realm_services(new fuchsia::sys::ServiceList);
    RealmArgs realm_args = RealmArgs::MakeWithAdditionalServices(
        nullptr, "test", "/data", "/data/cache", "/tmp", std::move(environment_services), false,
        std::move(root_realm_services), fuchsia::sys::EnvironmentOptions{}, std::move(dirfd));
    return Realm::Create(std::move(realm_args));
  }

  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<fuchsia::sys::internal::ComponentEventListener> fake_listener_service_;
  std::unique_ptr<sys::testing::ComponentContextProvider> context_provider_;
};

TEST_F(ComponentEventProviderTest, NotificationAfterShutdownDoesNotCrash) {
  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  fxl::UniqueFD dirfd(open(dir.c_str(), O_RDONLY));
  std::unique_ptr<Realm> realm = CreateTestRealm(std::move(dirfd));

  fuchsia::sys::internal::ComponentEventListenerPtr client;
  context_provider_->ConnectToPublicService(client.NewRequest(dispatcher()), kListenerName);

  {
    ComponentEventProviderImpl event_provider(realm.get(), dispatcher());
    event_provider.SetListener(std::move(client));
    // Let event_provider go out of scope on purpose while still having a listener.
  }
  // Drain events to force the listener callback to fire and try to send notifications to the
  // expired event_provider.
  RunLoopUntilIdle();
}

}  // namespace
}  // namespace component
