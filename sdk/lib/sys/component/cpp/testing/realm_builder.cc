// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/component/decl/cpp/fidl.h>
#include <fuchsia/component/test/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/component/cpp/testing/internal/errors.h>
#include <lib/sys/component/cpp/testing/internal/mock_runner.h>
#include <lib/sys/component/cpp/testing/internal/realm.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/component/cpp/testing/scoped_child.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/assert.h>

#include <memory>
#include <variant>

namespace sys {
namespace testing {

namespace {

constexpr char kCollectionName[] = "fuchsia_component_test_collection";
constexpr char kFrameworkIntermediaryChildName[] = "realm_builder_server";

void PanicIfMonikerBad(Moniker& moniker) {
  if (!moniker.path.empty()) {
    ZX_ASSERT_MSG(moniker.path.front() != '/', "Moniker %s is invalid. It contains a leading slash",
                  moniker.path.data());
    ZX_ASSERT_MSG(moniker.path.back() != '/', "Moniker %s is invalid. It contains a trailing slash",
                  moniker.path.data());
  }
}

// Basic implementation of std::get_if (since C++17).
// This function is namespaced with `cpp17` prefix because
// the name `get_if` clashes with std:: namespace usage *when* this
// library is compiled in C++17+.
// TODO(yaneury): Implement this in stdcompat library.
template <class T, class... Ts>
constexpr std::add_pointer_t<T> cpp17_get_if(cpp17::variant<Ts...>* pv) noexcept {
  return pv && cpp17::holds_alternative<T>(*pv) ? std::addressof(cpp17::get<T, Ts...>(*pv))
                                                : nullptr;
}

fuchsia::component::test::RouteEndpoint ConvertToFidl(Endpoint endpoint) {
  if (auto moniker = cpp17_get_if<Moniker>(&endpoint)) {
    return fuchsia::component::test::RouteEndpoint::WithComponent(std::string(moniker->path));
  }
  if (auto _ = cpp17_get_if<AboveRoot>(&endpoint)) {
    return fuchsia::component::test::RouteEndpoint::WithAboveRoot(
        fuchsia::component::test::AboveRoot());
  }

  ZX_PANIC("ConvertToFidl(Endpoint) reached unreachable block!");
}

fuchsia::component::test::Capability ConvertToFidl(Capability capability) {
  if (auto protocol = cpp17_get_if<Protocol>(&capability)) {
    fuchsia::component::test::ProtocolCapability fidl_capability;
    fidl_capability.set_name(std::string(protocol->name));
    return fuchsia::component::test::Capability::WithProtocol(std::move(fidl_capability));
  }
  if (auto directory = cpp17_get_if<Directory>(&capability)) {
    fuchsia::component::test::DirectoryCapability fidl_capability;
    fidl_capability.set_name(std::string(directory->name));
    fidl_capability.set_path(std::string(directory->path));
    fidl_capability.set_rights(std::move(directory->rights));
    return fuchsia::component::test::Capability::WithDirectory(std::move(fidl_capability));
  }
  if (auto storage = cpp17_get_if<Storage>(&capability)) {
    fuchsia::component::test::StorageCapability fidl_capability;
    fidl_capability.set_name(std::string(storage->name));
    fidl_capability.set_path(std::string(storage->path));
    return fuchsia::component::test::Capability::WithStorage(std::move(fidl_capability));
  }

  ZX_PANIC("ConvertToFidl(Capability) reached unreachable block!");
}

fuchsia::component::test::Component ConvertToFidl(Source source) {
  fuchsia::component::test::Component result;
  if (auto url = cpp17_get_if<ComponentUrl>(&source)) {
    return fuchsia::component::test::Component::WithUrl(std::string(url->url));
  }
  if (auto url = cpp17_get_if<LegacyComponentUrl>(&source)) {
    return fuchsia::component::test::Component::WithLegacyUrl(std::string(url->url));
  }

  ZX_PANIC("ConvertToFidl(Source) reached unreachable block!");
}

fuchsia::component::test::CapabilityRoute ConvertToFidl(CapabilityRoute route) {
  fuchsia::component::test::CapabilityRoute result;
  result.set_capability(ConvertToFidl(route.capability));
  result.set_source(ConvertToFidl(route.source));
  std::vector<fuchsia::component::test::RouteEndpoint> targets;
  for (const Endpoint& target : route.targets) {
    targets.push_back(ConvertToFidl(target));
  }
  result.set_targets(std::move(targets));
  return result;
}

fidl::InterfaceHandle<fuchsia::io::Directory> CreatePkgDirHandle() {
  int fd;
  ZX_SYS_ASSERT_STATUS_OK(
      "fdio_open_fd",
      fdio_open_fd("/pkg", fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                   &fd));
  zx_handle_t handle;
  ZX_SYS_ASSERT_STATUS_OK("fdio_fd_transfer", fdio_fd_transfer(fd, &handle));
  auto channel = zx::channel(handle);
  return fidl::InterfaceHandle<fuchsia::io::Directory>(std::move(channel));
}

}  // namespace

Realm::Realm(ScopedChild root, std::unique_ptr<internal::MockRunner> mock_runner)
    : root_(std::move(root)), mock_runner_(std::move(mock_runner)) {}

zx_status_t Realm::Connect(const std::string& interface_name, zx::channel request) const {
  return root_.Connect(interface_name, std::move(request));
}

std::string Realm::GetChildName() const { return root_.GetChildName(); }

Realm::Builder::Builder(fuchsia::component::RealmSyncPtr realm_proxy,
                        fuchsia::component::test::RealmBuilderSyncPtr realm_builder_proxy,
                        sys::ServiceDirectory realm_builder_exposed_dir,
                        std::unique_ptr<internal::MockRunner> mock_runner_server)
    : realm_commited_(false),
      realm_proxy_(std::move(realm_proxy)),
      realm_builder_proxy_(std::move(realm_builder_proxy)),
      realm_builder_exposed_dir_(std::move(realm_builder_exposed_dir)),
      mock_runner_(std::move(mock_runner_server)) {}

Realm::Builder& Realm::Builder::AddComponent(Moniker moniker, Component component) {
  PanicIfMonikerBad(moniker);
  {
    bool exists;
    ZX_SYS_ASSERT_STATUS_OK("FrameworkIntemediary/Contains",
                            realm_builder_proxy_->Contains(std::string(moniker.path), &exists));
    if (exists) {
      ZX_PANIC("Attempted to add a moniker that already exists in Realm: '%s'",
               moniker.path.data());
    }
  }
  if (auto mock = cpp17_get_if<Mock>(&component.source)) {
    fuchsia::component::test::RealmBuilder_SetMockComponent_Result result;
    ZX_SYS_ASSERT_STATUS_AND_RESULT_OK(
        "FrameworkIntemediary/SetMockComponent",
        realm_builder_proxy_->SetMockComponent(std::string(moniker.path), &result), result);
    mock_runner_->Register(result.response().mock_id, mock->impl);
  } else {
    fuchsia::component::test::RealmBuilder_SetComponent_Result result;
    ZX_SYS_ASSERT_STATUS_AND_RESULT_OK(
        "FrameworkIntemediary/SetComponent",
        realm_builder_proxy_->SetComponent(std::string(moniker.path),
                                           ConvertToFidl(component.source), &result),
        result);
  }
  if (component.eager) {
    fuchsia::component::test::RealmBuilder_MarkAsEager_Result result;
    ZX_SYS_ASSERT_STATUS_AND_RESULT_OK(
        "RealmBuilder/MarkAsEager",
        realm_builder_proxy_->MarkAsEager(std::string(moniker.path), &result), result);
  }
  return *this;
}

Realm::Builder& Realm::Builder::AddRoute(CapabilityRoute route) {
  fuchsia::component::test::RealmBuilder_RouteCapability_Result result;
  auto fidl_route = ConvertToFidl(route);
  ZX_SYS_ASSERT_STATUS_AND_RESULT_OK(
      "RealmBuilder/RouteCapability",
      realm_builder_proxy_->RouteCapability(std::move(fidl_route), &result), result);
  return *this;
}

Realm Realm::Builder::Build(async_dispatcher_t* dispatcher) {
  if (dispatcher == nullptr) {
    dispatcher = async_get_default_dispatcher();
  }
  ZX_SYS_ASSERT_NOT_NULL(dispatcher);
  ZX_ASSERT_MSG(!realm_commited_, "RealmBuilder::Build() called after Realm already created");
  fuchsia::component::test::RealmBuilder_Commit_Result result;
  ZX_SYS_ASSERT_STATUS_AND_RESULT_OK("FrameworkIntemediary/Commit",
                                     realm_builder_proxy_->Commit(&result), result);
  realm_commited_ = true;
  // Hand channel to async client so that MockRunner can listen to events.
  mock_runner_->Bind(realm_builder_proxy_.Unbind(), dispatcher);
  return Realm(ScopedChild::New(std::move(realm_proxy_), kCollectionName,
                                result.response().root_component_url),
               std::move(mock_runner_));
}

Realm::Builder Realm::Builder::Create(std::shared_ptr<sys::ServiceDirectory> svc) {
  fuchsia::component::test::RealmBuilderSyncPtr realm_builder_proxy;
  auto realm_proxy = internal::CreateRealmPtr(std::move(svc));
  auto child_ref = fuchsia::component::decl::ChildRef{.name = kFrameworkIntermediaryChildName};
  auto exposed_dir = internal::OpenExposedDir(realm_proxy.get(), child_ref);
  exposed_dir.Connect(realm_builder_proxy.NewRequest());
  fuchsia::component::test::RealmBuilder_Init_Result result;
  ZX_SYS_ASSERT_STATUS_AND_RESULT_OK(
      "RealmBuilder/Init", realm_builder_proxy->Init(CreatePkgDirHandle(), &result), result);
  return Builder(std::move(realm_proxy), std::move(realm_builder_proxy), std::move(exposed_dir),
                 std::make_unique<internal::MockRunner>());
}

}  // namespace testing
}  // namespace sys
