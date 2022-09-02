// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/decl/cpp/fidl.h>
#include <fuchsia/component/test/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>

#include <vector>

#ifndef LIB_SYS_COMPONENT_CPP_TESTING_INTERNAL_CONVERT_H_
#define LIB_SYS_COMPONENT_CPP_TESTING_INTERNAL_CONVERT_H_

namespace component_testing {
namespace internal {

fuchsia::component::test::ChildOptions ConvertToFidl(const ChildOptions& options);
fuchsia::component::decl::Ref ConvertToFidl(Ref ref);
fuchsia::component::test::Capability ConvertToFidl(Capability capability);

template <class Input, class Output>
std::vector<Output> ConvertToFidlVec(std::vector<Input> inputs) {
  std::vector<Output> result;
  result.reserve(inputs.size());
  for (const Input& input : inputs) {
    result.push_back(ConvertToFidl(input));
  }
  return result;
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
}  // namespace internal
}  // namespace component_testing

#endif  // LIB_SYS_COMPONENT_CPP_TESTING_INTERNAL_CONVERT_H_
