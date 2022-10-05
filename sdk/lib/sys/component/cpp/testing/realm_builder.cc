// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/config/cpp/fidl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/component/decl/cpp/fidl.h>
#include <fuchsia/component/runner/cpp/fidl.h>
#include <fuchsia/component/test/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/component/cpp/testing/internal/convert.h>
#include <lib/sys/component/cpp/testing/internal/errors.h>
#include <lib/sys/component/cpp/testing/internal/local_component_runner.h>
#include <lib/sys/component/cpp/testing/internal/realm.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/component/cpp/testing/scoped_child.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/assert.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace component_testing {
namespace {
constexpr char kCollectionName[] = "realm_builder";
constexpr char kFrameworkIntermediaryChildName[] = "realm_builder_server";
constexpr char kChildPathSeparator[] = "/";

fidl::InterfaceHandle<fuchsia::io::Directory> CreatePkgDirHandle() {
  int fd;
  ZX_COMPONENT_ASSERT_STATUS_OK(
      "fdio_open_fd", fdio_open_fd("/pkg",
                                   static_cast<uint32_t>(fuchsia::io::OpenFlags::RIGHT_READABLE |
                                                         fuchsia::io::OpenFlags::RIGHT_EXECUTABLE),
                                   &fd));
  zx_handle_t handle;
  ZX_COMPONENT_ASSERT_STATUS_OK("fdio_fd_transfer", fdio_fd_transfer(fd, &handle));
  auto channel = zx::channel(handle);
  return fidl::InterfaceHandle<fuchsia::io::Directory>(std::move(channel));
}

}  // namespace

// Implementation methods for Realm.

Realm& Realm::AddChild(const std::string& child_name, const std::string& url,
                       ChildOptions options) {
  fuchsia::component::test::Realm_AddChild_Result result;
  ZX_COMPONENT_ASSERT_STATUS_AND_RESULT_OK(
      "Realm/AddChild",
      realm_proxy_->AddChild(child_name, url, internal::ConvertToFidl(options), &result), result);
  return *this;
}

Realm& Realm::AddLegacyChild(const std::string& child_name, const std::string& url,
                             ChildOptions options) {
  fuchsia::component::test::Realm_AddLegacyChild_Result result;
  ZX_COMPONENT_ASSERT_STATUS_AND_RESULT_OK(
      "Realm/AddLegacyChild",
      realm_proxy_->AddLegacyChild(child_name, url, internal::ConvertToFidl(options), &result),
      result);
  return *this;
}

Realm& Realm::AddLocalChild(const std::string& child_name, LocalComponent* local_impl,
                            ChildOptions options) {
  return AddLocalChildImpl(child_name, LocalComponentImpl(local_impl), options);
}

Realm& Realm::AddLocalChild(const std::string& child_name, LocalComponentFactory local_impl,
                            ChildOptions options) {
  return AddLocalChildImpl(child_name, LocalComponentImpl(std::move(local_impl)), options);
}

Realm& Realm::AddLocalChildImpl(const std::string& child_name, LocalComponentImpl local_impl,
                                ChildOptions options) {
  if (cpp17::holds_alternative<LocalComponent*>(local_impl)) {
    ZX_SYS_ASSERT_NOT_NULL(cpp17::get<LocalComponent*>(local_impl));
  }
  runner_builder_->Register(GetResolvedName(child_name), std::move(local_impl));
  fuchsia::component::test::Realm_AddLocalChild_Result result;
  ZX_COMPONENT_ASSERT_STATUS_AND_RESULT_OK(
      "Realm/AddLocalChild",
      realm_proxy_->AddLocalChild(child_name, internal::ConvertToFidl(options), &result), result);
  return *this;
}

Realm Realm::AddChildRealm(const std::string& child_name, ChildOptions options) {
  fuchsia::component::test::RealmSyncPtr sub_realm_proxy;
  std::vector<std::string> sub_realm_scope = scope_;
  sub_realm_scope.push_back(child_name);
  Realm sub_realm(std::move(sub_realm_proxy), runner_builder_, std::move(sub_realm_scope));

  fuchsia::component::test::Realm_AddChildRealm_Result result;
  ZX_COMPONENT_ASSERT_STATUS_AND_RESULT_OK(
      "Realm/AddChildRealm",
      realm_proxy_->AddChildRealm(child_name, internal::ConvertToFidl(options),
                                  sub_realm.realm_proxy_.NewRequest(), &result),
      result);
  return sub_realm;
}

Realm& Realm::AddRoute(Route route) {
  auto capabilities = internal::ConvertToFidlVec<Capability, fuchsia::component::test::Capability>(
      route.capabilities);
  auto source = internal::ConvertToFidl(route.source);
  auto target = internal::ConvertToFidlVec<Ref, fuchsia::component::decl::Ref>(route.targets);

  fuchsia::component::test::Realm_AddRoute_Result result;
  ZX_COMPONENT_ASSERT_STATUS_AND_RESULT_OK(
      "Realm/AddRoute",
      realm_proxy_->AddRoute(std::move(capabilities), std::move(source), std::move(target),
                             &result),
      result);
  return *this;
}

Realm& Realm::RouteReadOnlyDirectory(const std::string& name, std::vector<Ref> to,
                                     DirectoryContents directory) {
  auto to_fidl = internal::ConvertToFidlVec<Ref, fuchsia::component::decl::Ref>(std::move(to));
  auto directory_fidl = directory.TakeAsFidl();

  fuchsia::component::test::Realm_ReadOnlyDirectory_Result result;
  ZX_COMPONENT_ASSERT_STATUS_AND_RESULT_OK(
      "Realm/ReadOnlyDirectory",
      realm_proxy_->ReadOnlyDirectory(name, std::move(to_fidl), std::move(directory_fidl), &result),
      result);

  return *this;
}

#if __Fuchsia_API_level__ >= 9
Realm& Realm::InitMutableConfigFromPackage(const std::string& name) {
  fuchsia::component::test::Realm_InitMutableConfigFromPackage_Result result;
  ZX_COMPONENT_ASSERT_STATUS_AND_RESULT_OK(
      "Realm/InitMutableConfigFromPackage",
      realm_proxy_->InitMutableConfigFromPackage(name, &result), result);
  return *this;
}
#endif

#if __Fuchsia_API_level__ >= 9
Realm& Realm::InitMutableConfigToEmpty(const std::string& name) {
  fuchsia::component::test::Realm_InitMutableConfigToEmpty_Result result;
  ZX_COMPONENT_ASSERT_STATUS_AND_RESULT_OK("Realm/InitMutableConfigToEmpty",
                                           realm_proxy_->InitMutableConfigToEmpty(name, &result),
                                           result);
  return *this;
}
#endif

Realm& Realm::SetConfigValue(const std::string& name, const std::string& key, ConfigValue value) {
  // TODO(https://fxbug.dev/103951) switch to SetConfigValue
  fuchsia::component::test::Realm_ReplaceConfigValue_Result result;
  ZX_COMPONENT_ASSERT_STATUS_AND_RESULT_OK(
      "Realm/SetConfigValue",
      realm_proxy_->ReplaceConfigValue(name, key, value.TakeAsFidl(), &result), result);
  return *this;
}

void Realm::ReplaceComponentDecl(const std::string& child_name,
                                 fuchsia::component::decl::Component decl) {
  fuchsia::component::test::Realm_ReplaceComponentDecl_Result result;
  ZX_COMPONENT_ASSERT_STATUS_AND_RESULT_OK(
      "Realm/ReplaceComponentDecl",
      realm_proxy_->ReplaceComponentDecl(child_name, std::move(decl), &result), result);
}

void Realm::ReplaceRealmDecl(fuchsia::component::decl::Component decl) {
  fuchsia::component::test::Realm_ReplaceRealmDecl_Result result;
  ZX_COMPONENT_ASSERT_STATUS_AND_RESULT_OK(
      "Realm/ReplaceRealmDecl", realm_proxy_->ReplaceRealmDecl(std::move(decl), &result), result);
}

fuchsia::component::decl::Component Realm::GetComponentDecl(const std::string& child_name) {
  fuchsia::component::test::Realm_GetComponentDecl_Result result;
  ZX_COMPONENT_ASSERT_STATUS_AND_RESULT_OK(
      "Realm/GetComponentDecl", realm_proxy_->GetComponentDecl(child_name, &result), result);

  return std::move(result.response().component_decl);
}

fuchsia::component::decl::Component Realm::GetRealmDecl() {
  fuchsia::component::test::Realm_GetRealmDecl_Result result;
  ZX_COMPONENT_ASSERT_STATUS_AND_RESULT_OK("Realm/GetRealmDecl",
                                           realm_proxy_->GetRealmDecl(&result), result);

  return std::move(result.response().component_decl);
}

Realm::Realm(fuchsia::component::test::RealmSyncPtr realm_proxy,
             std::shared_ptr<internal::LocalComponentRunner::Builder> runner_builder,
             std::vector<std::string> scope)
    : realm_proxy_(std::move(realm_proxy)),
      runner_builder_(std::move(runner_builder)),
      scope_(std::move(scope)) {}

std::string Realm::GetResolvedName(const std::string& child_name) {
  if (scope_.empty()) {
    return child_name;
  }

  std::stringstream path;
  for (const auto& s : scope_) {
    path << s << kChildPathSeparator;
  }
  return path.str() + child_name;
}

// Implementation methods for RealmBuilder.

RealmBuilder RealmBuilder::Create(std::shared_ptr<sys::ServiceDirectory> svc) {
  return CreateImpl(cpp17::nullopt, std::move(svc));
}

RealmBuilder RealmBuilder::CreateFromRelativeUrl(std::string_view relative_url,
                                                 std::shared_ptr<sys::ServiceDirectory> svc) {
  return CreateImpl(relative_url, std::move(svc));
}

RealmBuilder RealmBuilder::CreateImpl(cpp17::optional<std::string_view> relative_url,
                                      std::shared_ptr<sys::ServiceDirectory> svc) {
  if (svc == nullptr) {
    svc = sys::ServiceDirectory::CreateFromNamespace();
  }

  fuchsia::component::test::RealmBuilderFactorySyncPtr factory_proxy;
  auto realm_proxy = internal::CreateRealmPtr(svc);
  auto child_ref = fuchsia::component::decl::ChildRef{.name = kFrameworkIntermediaryChildName};
  auto exposed_dir = internal::OpenExposedDir(realm_proxy.get(), child_ref);
  exposed_dir.Connect(factory_proxy.NewRequest());
  fuchsia::component::test::BuilderSyncPtr builder_proxy;
  fuchsia::component::test::RealmSyncPtr test_realm_proxy;
  if (relative_url.has_value()) {
    ZX_ASSERT_MSG(!relative_url.value().empty(), "relative_url can't be empty");

    fuchsia::component::test::RealmBuilderFactory_CreateFromRelativeUrl_Result result;
    ZX_COMPONENT_ASSERT_STATUS_AND_RESULT_OK(
        "RealmBuilderFactory/CreateFromRelativeUrl",
        factory_proxy->CreateFromRelativeUrl(CreatePkgDirHandle(), relative_url.value().data(),
                                             test_realm_proxy.NewRequest(),
                                             builder_proxy.NewRequest(), &result),
        result);
  } else {
    fuchsia::component::test::RealmBuilderFactory_Create_Result result;
    ZX_COMPONENT_ASSERT_STATUS_AND_RESULT_OK(
        "RealmBuilderFactory/Create",
        factory_proxy->Create(CreatePkgDirHandle(), test_realm_proxy.NewRequest(),
                              builder_proxy.NewRequest(), &result),
        result);
  }
  return RealmBuilder(svc, std::move(builder_proxy), std::move(test_realm_proxy));
}

RealmBuilder& RealmBuilder::AddChild(const std::string& child_name, const std::string& url,
                                     ChildOptions options) {
  ZX_ASSERT_MSG(!child_name.empty(), "child_name can't be empty");
  ZX_ASSERT_MSG(!url.empty(), "url can't be empty");

  root_.AddChild(child_name, url, options);
  return *this;
}

RealmBuilder& RealmBuilder::AddLegacyChild(const std::string& child_name, const std::string& url,
                                           ChildOptions options) {
  ZX_ASSERT_MSG(!child_name.empty(), "child_name can't be empty");
  ZX_ASSERT_MSG(!url.empty(), "url can't be empty");

  root_.AddLegacyChild(child_name, url, options);
  return *this;
}

RealmBuilder& RealmBuilder::AddLocalChild(const std::string& child_name, LocalComponent* local_impl,
                                          ChildOptions options) {
  ZX_ASSERT_MSG(!child_name.empty(), "child_name can't be empty");
  ZX_ASSERT_MSG(local_impl != nullptr, "local_impl can't be nullptr");
  root_.AddLocalChildImpl(child_name, local_impl, options);
  return *this;
}

RealmBuilder& RealmBuilder::AddLocalChild(const std::string& child_name,
                                          LocalComponentFactory local_impl, ChildOptions options) {
  ZX_ASSERT_MSG(!child_name.empty(), "child_name can't be empty");
  root_.AddLocalChildImpl(child_name, LocalComponentImpl(std::move(local_impl)), options);
  return *this;
}

Realm RealmBuilder::AddChildRealm(const std::string& child_name, ChildOptions options) {
  ZX_ASSERT_MSG(!child_name.empty(), "child_name can't be empty");
  return root_.AddChildRealm(child_name, options);
}

RealmBuilder& RealmBuilder::AddRoute(Route route) {
  ZX_ASSERT_MSG(!route.capabilities.empty(), "route.capabilities can't be empty");
  ZX_ASSERT_MSG(!route.targets.empty(), "route.targets can't be empty");

  root_.AddRoute(std::move(route));
  return *this;
}

RealmBuilder& RealmBuilder::RouteReadOnlyDirectory(const std::string& name, std::vector<Ref> to,
                                                   DirectoryContents directory) {
  root_.RouteReadOnlyDirectory(name, std::move(to), std::move(directory));
  return *this;
}

#if __Fuchsia_API_level__ >= 9
RealmBuilder& RealmBuilder::InitMutableConfigFromPackage(const std::string& name) {
  root_.InitMutableConfigFromPackage(name);
  return *this;
}
#endif

#if __Fuchsia_API_level__ >= 9
RealmBuilder& RealmBuilder::InitMutableConfigToEmpty(const std::string& name) {
  root_.InitMutableConfigToEmpty(name);
  return *this;
}
#endif

RealmBuilder& RealmBuilder::SetConfigValue(const std::string& name, const std::string& key,
                                           ConfigValue value) {
  root_.SetConfigValue(name, key, std::move(value));
  return *this;
}

void RealmBuilder::ReplaceComponentDecl(const std::string& child_name,
                                        fuchsia::component::decl::Component decl) {
  root_.ReplaceComponentDecl(child_name, std::move(decl));
}

void RealmBuilder::ReplaceRealmDecl(fuchsia::component::decl::Component decl) {
  root_.ReplaceRealmDecl(std::move(decl));
}

fuchsia::component::decl::Component RealmBuilder::GetComponentDecl(const std::string& child_name) {
  return root_.GetComponentDecl(child_name);
}

fuchsia::component::decl::Component RealmBuilder::GetRealmDecl() { return root_.GetRealmDecl(); }

RealmRoot RealmBuilder::Build(async_dispatcher_t* dispatcher) {
  ZX_ASSERT_MSG(!realm_commited_, "Builder::Build() called after Realm already created");
  if (dispatcher == nullptr) {
    dispatcher = async_get_default_dispatcher();
  }
  ZX_ASSERT_MSG(dispatcher != nullptr, "Builder::Build() called without configured dispatcher");
  auto local_component_runner = runner_builder_->Build(dispatcher);
  fuchsia::component::test::Builder_Build_Result result;
  ZX_COMPONENT_ASSERT_STATUS_AND_RESULT_OK(
      "Builder/Build", builder_proxy_->Build(local_component_runner->NewBinding(), &result),
      result);
  realm_commited_ = true;

  auto scoped_child = ScopedChild::New(kCollectionName, result.response().root_component_url, svc_);
  // Connect to fuchsia.component.Binder to automatically start Realm.
  scoped_child.ConnectSync<fuchsia::component::Binder>();

  auto teardown_status = std::make_shared<RealmRoot::TeardownStatus>(false);
  std::weak_ptr<RealmRoot::TeardownStatus> weak_teardown_status = teardown_status;
  // TODO(108814): Instead of implementing this callback in RealmBuilder, we could implement it in
  // the underlying ScopedChild.
  auto callback = [weak_teardown_status = std::move(weak_teardown_status)](
                      cpp17::optional<fuchsia::component::Error> err) {
    if (std::shared_ptr<RealmRoot::TeardownStatus> const p = weak_teardown_status.lock()) {
      if (err.has_value()) {
        *p = *err;
      } else {
        *p = true;
      }
    }
  };
  scoped_child.MakeTeardownAsync(dispatcher, callback);

  return RealmRoot(std::move(teardown_status), std::move(local_component_runner),
                   std::move(scoped_child));
}

Realm& RealmBuilder::root() { return root_; }

RealmBuilder::RealmBuilder(std::shared_ptr<sys::ServiceDirectory> svc,
                           fuchsia::component::test::BuilderSyncPtr builder_proxy,
                           fuchsia::component::test::RealmSyncPtr test_realm_proxy)
    : svc_(std::move(svc)),
      builder_proxy_(std::move(builder_proxy)),
      runner_builder_(std::make_shared<internal::LocalComponentRunner::Builder>()),
      root_(Realm(std::move(test_realm_proxy), runner_builder_)) {}

// Implementation methods for RealmRoot.

RealmRoot::RealmRoot(std::shared_ptr<TeardownStatus> teardown_status,
                     std::unique_ptr<internal::LocalComponentRunner> local_component_runner,
                     ScopedChild root)
    : teardown_callback_([teardown_status = std::move(teardown_status)] {
        ZX_ASSERT_MSG(!cpp17::holds_alternative<fuchsia::component::Error>(*teardown_status),
                      "Realm teardown failed");
        return cpp17::get<bool>(*teardown_status);
      }),
      local_component_runner_(std::move(local_component_runner)),
      root_(std::move(root)) {}

zx_status_t RealmRoot::Connect(const std::string& interface_name, zx::channel request) const {
  return root_.Connect(interface_name, std::move(request));
}

std::string RealmRoot::GetChildName() const { return root_.GetChildName(); }

#if __Fuchsia_API_level__ >= 9
fit::function<bool()> RealmRoot::TeardownCallback() {
  ZX_ASSERT_MSG(teardown_callback_,
                "RealmRoot::TeardownCallback() has already been called or RealmRoot was moved");
  return std::move(teardown_callback_);
}
#endif

}  // namespace component_testing
