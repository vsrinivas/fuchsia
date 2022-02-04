// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/component/cpp/testing/internal/convert.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>

namespace component_testing {
namespace internal {

// Convenience macro to check if a std::optional |field| is present
// and if so, populate the |fidl_type| with it. This is only
// used in |ConvertToFidl|.
#define ZX_COMPONENT_ADD_IF_PRESENT(cpp_type, field, fidl_type) \
  if ((cpp_type)->field.has_value()) {                          \
    (fidl_type).set_##field((cpp_type)->field.value());         \
  }

// Same as above but wraps |field| value in a std::string.
#define ZX_COMPONENT_ADD_STR_IF_PRESENT(cpp_type, field, fidl_type)  \
  if ((cpp_type)->field.has_value()) {                               \
    (fidl_type).set_##field(std::string((cpp_type)->field.value())); \
  }

namespace {
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
}  // namespace

fuchsia::component::test::ChildOptions ConvertToFidl(const ChildOptions& options) {
  fuchsia::component::test::ChildOptions result;
  result.set_startup(options.startup_mode);
  if (!options.environment.empty()) {
    result.set_environment(std::string(options.environment));
  }

  return result;
}

fuchsia::component::decl::Ref ConvertToFidl(Ref ref) {
  if (auto child_ref = cpp17_get_if<ChildRef>(&ref)) {
    fuchsia::component::decl::ChildRef result;
    result.name = std::string(child_ref->name);
    return fuchsia::component::decl::Ref::WithChild(std::move(result));
  }
  if (auto _ = cpp17_get_if<ParentRef>(&ref)) {
    return fuchsia::component::decl::Ref::WithParent(fuchsia::component::decl::ParentRef());
  }

  ZX_PANIC("ConvertToFidl(Ref) reached unreachable block!");
}

fuchsia::component::test::Capability2 ConvertToFidl(Capability capability) {
  if (auto protocol = cpp17_get_if<Protocol>(&capability)) {
    fuchsia::component::test::Protocol fidl_capability;

    fidl_capability.set_name(std::string(protocol->name));
    ZX_COMPONENT_ADD_STR_IF_PRESENT(protocol, as, fidl_capability);
    ZX_COMPONENT_ADD_STR_IF_PRESENT(protocol, path, fidl_capability);
    ZX_COMPONENT_ADD_IF_PRESENT(protocol, type, fidl_capability);

    return fuchsia::component::test::Capability2::WithProtocol(std::move(fidl_capability));
  }
  if (auto service = cpp17_get_if<Service>(&capability)) {
    fuchsia::component::test::Service fidl_capability;

    fidl_capability.set_name(std::string(service->name));
    ZX_COMPONENT_ADD_STR_IF_PRESENT(service, as, fidl_capability);
    ZX_COMPONENT_ADD_STR_IF_PRESENT(service, path, fidl_capability);

    return fuchsia::component::test::Capability2::WithService(std::move(fidl_capability));
  }
  if (auto directory = cpp17_get_if<Directory>(&capability)) {
    fuchsia::component::test::Directory fidl_capability;

    fidl_capability.set_name(std::string(directory->name));
    ZX_COMPONENT_ADD_STR_IF_PRESENT(directory, as, fidl_capability);
    ZX_COMPONENT_ADD_IF_PRESENT(directory, type, fidl_capability);
    ZX_COMPONENT_ADD_STR_IF_PRESENT(directory, subdir, fidl_capability);
    ZX_COMPONENT_ADD_IF_PRESENT(directory, rights, fidl_capability);
    ZX_COMPONENT_ADD_STR_IF_PRESENT(directory, path, fidl_capability);

    return fuchsia::component::test::Capability2::WithDirectory(std::move(fidl_capability));
  }

  ZX_PANIC("ConvertToFidl(Capability) reached unreachable block!");
}

}  // namespace internal
}  // namespace component_testing
