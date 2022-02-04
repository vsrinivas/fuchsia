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
fuchsia::component::test::Capability2 ConvertToFidl(Capability capability);

template <class Input, class Output>
std::vector<Output> ConvertToFidlVec(std::vector<Input> inputs) {
  std::vector<Output> result;
  result.reserve(inputs.size());
  for (const Input& input : inputs) {
    result.push_back(ConvertToFidl(input));
  }
  return result;
}
}  // namespace internal
}  // namespace component_testing

#endif  // LIB_SYS_COMPONENT_CPP_TESTING_INTERNAL_CONVERT_H_
