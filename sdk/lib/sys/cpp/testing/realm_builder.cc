// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/realm/builder/cpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/internal/errors.h>
#include <lib/sys/cpp/testing/internal/realm.h>
#include <lib/sys/cpp/testing/internal/scoped_instance.h>
#include <lib/sys/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/log_level.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <algorithm>
#include <utility>
#include <variant>

namespace sys::testing {

namespace {

constexpr char kCollectionName[] = "fuchsia_component_test_collection";
constexpr char kFrameworkIntermediaryChildName[] = "fuchsia_component_test_framework_intermediary";
constexpr char kRootName[] = "sys_cpp_realm_builder_root";

void PanicIfMonikerBad(Moniker& moniker) {
  if (!moniker.path.empty()) {
    ZX_ASSERT_MSG(moniker.path.front() != '/', "Moniker %s is invalid. It contains a leading slash",
                  moniker.path.c_str());
    ZX_ASSERT_MSG(moniker.path.back() != '/', "Moniker %s is invalid. It contains a trailing slash",
                  moniker.path.c_str());
  }
}

fuchsia::realm::builder::RouteEndpoint ConvertToFidl(Endpoint endpoint) {
  if (auto moniker = std::get_if<Moniker>(&endpoint)) {
    return fuchsia::realm::builder::RouteEndpoint::WithComponent(std::move(moniker->path));
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
    fidl_capability.set_name(std::move(protocol->name));
    return fuchsia::realm::builder::Capability::WithProtocol(std::move(fidl_capability));
  }
  if (auto directory = std::get_if<Directory>(&capability)) {
    fuchsia::realm::builder::DirectoryCapability fidl_capability;
    fidl_capability.set_name(std::move(directory->name));
    fidl_capability.set_path(std::move(directory->path));
    fidl_capability.set_rights(std::move(directory->rights));
    return fuchsia::realm::builder::Capability::WithDirectory(std::move(fidl_capability));
  }
  if (auto storage = std::get_if<Storage>(&capability)) {
    fuchsia::realm::builder::StorageCapability fidl_capability;
    fidl_capability.set_name(std::move(storage->name));
    fidl_capability.set_path(std::move(storage->path));
    return fuchsia::realm::builder::Capability::WithStorage(std::move(fidl_capability));
  }

  ZX_PANIC("ConvertToFidl(Capability) reached unreachable block!");
}

fuchsia::realm::builder::Component ConvertToFidl(Source source) {
  fuchsia::realm::builder::Component result;
  if (auto url = std::get_if<ComponentUrl>(&source)) {
    return fuchsia::realm::builder::Component::WithUrl(std::move(url->url));
  }
  if (auto url = std::get_if<LegacyComponentUrl>(&source)) {
    return fuchsia::realm::builder::Component::WithLegacyUrl(std::move(url->url));
  }

  ZX_PANIC("ConvertToFidl(Source) reached unreachable block!");
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

}  // namespace

Realm::Realm(internal::ScopedInstance root) : root_(std::move(root)) {}

Realm::Builder::Builder(
    fuchsia::realm::builder::FrameworkIntermediarySyncPtr framework_intermediary_proxy,
    sys::ServiceDirectory framework_intermediary_exposed_dir)
    : realm_commited_(false),
      framework_intermediary_proxy_(std::move(framework_intermediary_proxy)),
      framework_intermediary_exposed_dir_(std::move(framework_intermediary_exposed_dir)) {}

Realm::Builder& Realm::Builder::AddComponent(Moniker moniker, Component component) {
  PanicIfMonikerBad(moniker);
  {
    bool exists;
    ASSERT_STATUS_OK("FrameworkIntemediary/Contains",
                     framework_intermediary_proxy_->Contains(moniker.path, &exists));
    if (exists) {
      ZX_PANIC("Attempted to add a moniker that already exists in Realm: '%s'",
               moniker.path.c_str());
    }
  }
  {
    fuchsia::realm::builder::FrameworkIntermediary_SetComponent_Result result;
    ASSERT_STATUS_AND_RESULT_OK("FrameworkIntemediary/SetComponent",
                                framework_intermediary_proxy_->SetComponent(
                                    moniker.path, ConvertToFidl(component.source), &result),
                                result);
  }
  if (component.eager) {
    fuchsia::realm::builder::FrameworkIntermediary_MarkAsEager_Result result;
    ASSERT_STATUS_AND_RESULT_OK("FrameworkIntermediary/MarkAsEager",
                                framework_intermediary_proxy_->MarkAsEager(moniker.path, &result),
                                result);
  }
  return *this;
}

Realm::Builder& Realm::Builder::AddRoute(CapabilityRoute route) {
  fuchsia::realm::builder::FrameworkIntermediary_RouteCapability_Result result;
  auto fidl_route = ConvertToFidl(std::move(route));
  ASSERT_STATUS_AND_RESULT_OK(
      "FrameworkIntermediary/RouteCapability",
      framework_intermediary_proxy_->RouteCapability(std::move(fidl_route), &result), result);
  return *this;
}

Realm Realm::Builder::Build(const sys::ComponentContext* context) {
  ZX_ASSERT_MSG(!realm_commited_, "RealmBuilder::Build() called after Realm already created");
  ZX_ASSERT_MSG(context != nullptr, "context passed to RealmBuilder::Build() must not be nullptr");
  fuchsia::realm::builder::FrameworkIntermediary_Commit_Result result;
  ASSERT_STATUS_AND_RESULT_OK("FrameworkIntemediary/Commit",
                              framework_intermediary_proxy_->Commit(&result), result);
  realm_commited_ = true;
  std::string root_component_url = result.response().root_component_url;
  return Realm(
      internal::ScopedInstance::New(context, kCollectionName, kRootName, root_component_url));
}

Realm::Builder Realm::Builder::New(const sys::ComponentContext* context) {
  ZX_ASSERT_MSG(context != nullptr, "context passed to RealmBuilder::New() must not be nullptr");
  fuchsia::realm::builder::FrameworkIntermediarySyncPtr proxy;
  auto realm = internal::CreateRealmPtr(context);
  auto child_ref = fuchsia::sys2::ChildRef{.name = kFrameworkIntermediaryChildName};
  auto exposed_dir = internal::BindChild(realm.get(), child_ref);
  exposed_dir.Connect(proxy.NewRequest());
  return Builder(std::move(proxy), std::move(exposed_dir));
}

}  // namespace sys::testing
