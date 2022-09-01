// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER2_NODE_ADD_ARGS_H_
#define LIB_DRIVER2_NODE_ADD_ARGS_H_

#include <fidl/fuchsia.component.decl/cpp/fidl.h>
#include <fidl/fuchsia.component.decl/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/fidl.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/fidl/cpp/wire/arena.h>
#include <lib/fidl/cpp/wire/traits.h>

#include <string_view>

namespace driver {

fuchsia_component_decl::Offer MakeOffer(std::string_view service_name,
                                        std::string_view instance_name);

template <typename Service>
fuchsia_component_decl::Offer MakeOffer(std::string_view instance_name) {
  static_assert(fidl::IsServiceV<Service>, "Service must be a fidl Service");
  return MakeOffer(Service::Name, instance_name);
}

fuchsia_component_decl::wire::Offer MakeOffer(fidl::AnyArena& arena, std::string_view service_name,
                                              std::string_view instance_name);
template <typename Service>
fuchsia_component_decl::wire::Offer MakeOffer(fidl::AnyArena& arena,
                                              std::string_view instance_name) {
  static_assert(fidl::IsServiceV<Service>, "Service must be a fidl Service");
  return MakeOffer(arena, Service::Name, instance_name);
}
}  // namespace driver

#endif  // LIB_DRIVER2_NODE_ADD_ARGS_H_
