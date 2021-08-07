// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/realm/builder/cpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/internal/errors.h>
#include <lib/sys/cpp/testing/internal/mock_runner.h>
#include <lib/sys/cpp/testing/internal/realm.h>
#include <lib/sys/cpp/testing/internal/scoped_instance.h>
#include <lib/sys/cpp/testing/realm_builder.h>
#include <lib/sys/cpp/testing/realm_builder_types.h>
#include <zircon/assert.h>

#include <memory>
#include <variant>

#include "lib/async/default.h"

namespace sys::testing {

namespace {

constexpr char kCollectionName[] = "fuchsia_component_test_collection";
constexpr char kFrameworkIntermediaryChildName[] = "fuchsia_component_test_framework_intermediary";
constexpr char kMockRunnerName[] = "realm_builder";
constexpr char kMockIdKey[] = "mock_id";

void PanicIfMonikerBad(Moniker& moniker) {
  if (!moniker.path.empty()) {
    ZX_ASSERT_MSG(moniker.path.front() != '/', "Moniker %s is invalid. It contains a leading slash",
                  moniker.path.data());
    ZX_ASSERT_MSG(moniker.path.back() != '/', "Moniker %s is invalid. It contains a trailing slash",
                  moniker.path.data());
  }
}

fuchsia::realm::builder::RouteEndpoint ConvertToFidl(Endpoint endpoint) {
  if (auto moniker = std::get_if<Moniker>(&endpoint)) {
    return fuchsia::realm::builder::RouteEndpoint::WithComponent(std::string(moniker->path));
  }
  if (auto _ = std::get_if<AboveRoot>(&endpoint)) {
    return fuchsia::realm::builder::RouteEndpoint::WithAboveRoot(
        fuchsia::realm::builder::AboveRoot());
  }

  ZX_PANIC("ConvertToFidl(Endpoint) reached unreachable block!");
}

fuchsia::realm::builder::Capability ConvertToFidl(Capability capability) {
  if (auto protocol = std::get_if<Protocol>(&capability)) {
    fuchsia::realm::builder::ProtocolCapability fidl_capability;
    fidl_capability.set_name(std::string(protocol->name));
    return fuchsia::realm::builder::Capability::WithProtocol(std::move(fidl_capability));
  }
  if (auto directory = std::get_if<Directory>(&capability)) {
    fuchsia::realm::builder::DirectoryCapability fidl_capability;
    fidl_capability.set_name(std::string(directory->name));
    fidl_capability.set_path(std::string(directory->path));
    fidl_capability.set_rights(std::move(directory->rights));
    return fuchsia::realm::builder::Capability::WithDirectory(std::move(fidl_capability));
  }
  if (auto storage = std::get_if<Storage>(&capability)) {
    fuchsia::realm::builder::StorageCapability fidl_capability;
    fidl_capability.set_name(std::string(storage->name));
    fidl_capability.set_path(std::string(storage->path));
    return fuchsia::realm::builder::Capability::WithStorage(std::move(fidl_capability));
  }

  ZX_PANIC("ConvertToFidl(Capability) reached unreachable block!");
}

fuchsia::realm::builder::Component ConvertToFidl(Source source) {
  fuchsia::realm::builder::Component result;
  if (auto url = std::get_if<ComponentUrl>(&source)) {
    return fuchsia::realm::builder::Component::WithUrl(std::string(url->url));
  }
  if (auto url = std::get_if<LegacyComponentUrl>(&source)) {
    return fuchsia::realm::builder::Component::WithLegacyUrl(std::string(url->url));
  }

  ZX_PANIC("ConvertToFidl(Source) reached unreachable block!");
}

fuchsia::realm::builder::Component CreateMockComponentFidl(std::string mock_id) {
  fuchsia::sys2::ComponentDecl component_decl;
  fuchsia::sys2::ProgramDecl program_decl;
  program_decl.set_runner(kMockRunnerName);
  fuchsia::data::Dictionary dictionary;
  fuchsia::data::DictionaryEntry entry;
  entry.key = kMockIdKey;
  auto value = fuchsia::data::DictionaryValue::New();
  value->set_str(std::move(mock_id));
  entry.value = std::move(value);
  dictionary.mutable_entries()->push_back(std::move(entry));
  program_decl.set_info(std::move(dictionary));
  component_decl.set_program(std::move(program_decl));
  return fuchsia::realm::builder::Component::WithDecl(std::move(component_decl));
}

fuchsia::realm::builder::CapabilityRoute ConvertToFidl(CapabilityRoute route) {
  fuchsia::realm::builder::CapabilityRoute result;
  result.set_capability(ConvertToFidl(route.capability));
  result.set_source(ConvertToFidl(route.source));
  std::vector<fuchsia::realm::builder::RouteEndpoint> targets;
  for (const Endpoint& target : route.targets) {
    targets.push_back(ConvertToFidl(target));
  }
  result.set_targets(std::move(targets));
  return result;
}

fidl::InterfaceHandle<fuchsia::io::Directory> CreatePkgDirHandle() {
  int fd;
  ASSERT_STATUS_OK(
      "fdio_open_fd",
      fdio_open_fd("/pkg", fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                   &fd));
  zx_handle_t handle;
  ASSERT_STATUS_OK("fdio_fd_transfer", fdio_fd_transfer(fd, &handle));
  auto channel = zx::channel(handle);
  return fidl::InterfaceHandle<fuchsia::io::Directory>(std::move(channel));
}

}  // namespace

Realm::Realm(internal::ScopedInstance root, std::unique_ptr<internal::MockRunner> mock_runner)
    : root_(std::move(root)), mock_runner_(std::move(mock_runner)) {}

std::string Realm::GetChildName() const { return root_.GetChildName(); }

Realm::Builder::Builder(
    fuchsia::sys2::RealmSyncPtr realm_proxy,
    fuchsia::realm::builder::FrameworkIntermediarySyncPtr framework_intermediary_proxy,
    sys::ServiceDirectory framework_intermediary_exposed_dir,
    std::unique_ptr<internal::MockRunner> mock_runner_server)
    : realm_commited_(false),
      realm_proxy_(std::move(realm_proxy)),
      framework_intermediary_proxy_(std::move(framework_intermediary_proxy)),
      framework_intermediary_exposed_dir_(std::move(framework_intermediary_exposed_dir)),
      mock_runner_(std::move(mock_runner_server)) {}

Realm::Builder& Realm::Builder::AddComponent(Moniker moniker, Component component) {
  PanicIfMonikerBad(moniker);
  {
    bool exists;
    ASSERT_STATUS_OK("FrameworkIntemediary/Contains",
                     framework_intermediary_proxy_->Contains(std::string(moniker.path), &exists));
    if (exists) {
      ZX_PANIC("Attempted to add a moniker that already exists in Realm: '%s'",
               moniker.path.data());
    }
  }
  if (auto mock = std::get_if<Mock>(&component.source)) {
    std::string mock_id;
    ASSERT_STATUS_OK("FrameworkIntermediary/NewMockId",
                     framework_intermediary_proxy_->NewMockId(&mock_id));
    fuchsia::realm::builder::FrameworkIntermediary_SetComponent_Result result;
    ASSERT_STATUS_AND_RESULT_OK(
        "FrameworkIntemediary/SetComponent",
        framework_intermediary_proxy_->SetComponent(std::string(moniker.path),
                                                    CreateMockComponentFidl(mock_id), &result),
        result);
    mock_runner_->Register(mock_id, mock->impl);
  } else {
    fuchsia::realm::builder::FrameworkIntermediary_SetComponent_Result result;
    ASSERT_STATUS_AND_RESULT_OK(
        "FrameworkIntemediary/SetComponent",
        framework_intermediary_proxy_->SetComponent(std::string(moniker.path),
                                                    ConvertToFidl(component.source), &result),
        result);
  }
  if (component.eager) {
    fuchsia::realm::builder::FrameworkIntermediary_MarkAsEager_Result result;
    ASSERT_STATUS_AND_RESULT_OK(
        "FrameworkIntermediary/MarkAsEager",
        framework_intermediary_proxy_->MarkAsEager(std::string(moniker.path), &result), result);
  }
  return *this;
}

Realm::Builder& Realm::Builder::AddRoute(CapabilityRoute route) {
  fuchsia::realm::builder::FrameworkIntermediary_RouteCapability_Result result;
  auto fidl_route = ConvertToFidl(route);
  ASSERT_STATUS_AND_RESULT_OK(
      "FrameworkIntermediary/RouteCapability",
      framework_intermediary_proxy_->RouteCapability(std::move(fidl_route), &result), result);
  return *this;
}

Realm Realm::Builder::Build(async_dispatcher_t* dispatcher) {
  if (dispatcher == nullptr) {
    dispatcher = async_get_default_dispatcher();
  }
  ASSERT_NOT_NULL(dispatcher);
  ZX_ASSERT_MSG(!realm_commited_, "RealmBuilder::Build() called after Realm already created");
  fuchsia::realm::builder::FrameworkIntermediary_Commit_Result result;
  ASSERT_STATUS_AND_RESULT_OK("FrameworkIntemediary/Commit",
                              framework_intermediary_proxy_->Commit(&result), result);
  realm_commited_ = true;
  // Hand channel to async client so that MockRunner can listen to events.
  mock_runner_->Bind(framework_intermediary_proxy_.Unbind(), dispatcher);
  return Realm(internal::ScopedInstance::New(std::move(realm_proxy_), kCollectionName,
                                             result.response().root_component_url),
               std::move(mock_runner_));
}

Realm::Builder Realm::Builder::New(const sys::ComponentContext* context) {
  ZX_ASSERT_MSG(context != nullptr, "context passed to RealmBuilder::New() must not be nullptr");
  fuchsia::realm::builder::FrameworkIntermediarySyncPtr framework_intermediary_proxy;
  auto realm_proxy = internal::CreateRealmPtr(context);
  auto child_ref = fuchsia::sys2::ChildRef{.name = kFrameworkIntermediaryChildName};
  auto exposed_dir = internal::OpenExposedDir(realm_proxy.get(), child_ref);
  exposed_dir.Connect(framework_intermediary_proxy.NewRequest());
  fuchsia::realm::builder::FrameworkIntermediary_Init_Result result;
  ASSERT_STATUS_AND_RESULT_OK("FrameworkIntermediary/Init",
                              framework_intermediary_proxy->Init(CreatePkgDirHandle(), &result),
                              result);
  return Builder(std::move(realm_proxy), std::move(framework_intermediary_proxy),
                 std::move(exposed_dir), std::make_unique<internal::MockRunner>());
}

}  // namespace sys::testing
