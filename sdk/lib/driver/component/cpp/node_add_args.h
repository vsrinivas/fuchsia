// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_COMPONENT_CPP_NODE_ADD_ARGS_H_
#define LIB_DRIVER_COMPONENT_CPP_NODE_ADD_ARGS_H_

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

inline fuchsia_driver_framework::NodeProperty MakeProperty(uint32_t key, uint32_t value) {
  return fuchsia_driver_framework::NodeProperty{
      {.key = fuchsia_driver_framework::NodePropertyKey::WithIntValue(key),
       .value = fuchsia_driver_framework::NodePropertyValue::WithIntValue(value)}};
}

inline fuchsia_driver_framework::NodeProperty MakeProperty(std::string_view key,
                                                           std::string_view value) {
  return fuchsia_driver_framework::NodeProperty{
      {.key = fuchsia_driver_framework::NodePropertyKey::WithStringValue(std::string(key)),
       .value = fuchsia_driver_framework::NodePropertyValue::WithStringValue(std::string(value))}};
}

inline fuchsia_driver_framework::NodeProperty MakeProperty(std::string_view key,
                                                           const char* value) {
  return MakeProperty(key, std::string_view(value));
}

inline fuchsia_driver_framework::NodeProperty MakeProperty(std::string_view key, bool value) {
  return fuchsia_driver_framework::NodeProperty{
      {.key = fuchsia_driver_framework::NodePropertyKey::WithStringValue(std::string(key)),
       .value = fuchsia_driver_framework::NodePropertyValue::WithBoolValue(value)}};
}

inline fuchsia_driver_framework::NodeProperty MakeProperty(std::string_view key, uint32_t value) {
  return fuchsia_driver_framework::NodeProperty{
      {.key = fuchsia_driver_framework::NodePropertyKey::WithStringValue(std::string(key)),
       .value = fuchsia_driver_framework::NodePropertyValue::WithIntValue(value)}};
}

inline fuchsia_driver_framework::NodeProperty MakeEnumProperty(std::string_view key,
                                                               std::string_view value) {
  return fuchsia_driver_framework::NodeProperty{
      {.key = fuchsia_driver_framework::NodePropertyKey::WithStringValue(std::string(key)),
       .value = fuchsia_driver_framework::NodePropertyValue::WithEnumValue(std::string(value))}};
}

inline fuchsia_driver_framework::wire::NodeProperty MakeProperty(fidl::AnyArena& arena,
                                                                 uint32_t key, uint32_t value) {
  return fuchsia_driver_framework::wire::NodeProperty::Builder(arena)
      .key(fuchsia_driver_framework::wire::NodePropertyKey::WithIntValue(key))
      .value(fuchsia_driver_framework::wire::NodePropertyValue::WithIntValue(value))
      .Build();
}

inline fuchsia_driver_framework::wire::NodeProperty MakeProperty(fidl::AnyArena& arena,
                                                                 std::string_view key,
                                                                 std::string_view value) {
  return fuchsia_driver_framework::wire::NodeProperty::Builder(arena)
      .key(fuchsia_driver_framework::wire::NodePropertyKey::WithStringValue(arena, key))
      .value(fuchsia_driver_framework::wire::NodePropertyValue::WithStringValue(arena, value))
      .Build();
}

inline fuchsia_driver_framework::wire::NodeProperty MakeProperty(fidl::AnyArena& arena,
                                                                 std::string_view key,
                                                                 const char* value) {
  return MakeProperty(arena, key, std::string_view(value));
}

inline fuchsia_driver_framework::wire::NodeProperty MakeProperty(fidl::AnyArena& arena,
                                                                 std::string_view key, bool value) {
  return fuchsia_driver_framework::wire::NodeProperty::Builder(arena)
      .key(fuchsia_driver_framework::wire::NodePropertyKey::WithStringValue(arena, key))
      .value(fuchsia_driver_framework::wire::NodePropertyValue::WithBoolValue(value))
      .Build();
}

inline fuchsia_driver_framework::wire::NodeProperty MakeProperty(fidl::AnyArena& arena,
                                                                 std::string_view key,
                                                                 uint32_t value) {
  return fuchsia_driver_framework::wire::NodeProperty::Builder(arena)
      .key(fuchsia_driver_framework::wire::NodePropertyKey::WithStringValue(arena, key))
      .value(fuchsia_driver_framework::wire::NodePropertyValue::WithIntValue(value))
      .Build();
}

inline fuchsia_driver_framework::wire::NodeProperty MakeEnumProperty(fidl::AnyArena& arena,
                                                                     std::string_view key,
                                                                     std::string_view value) {
  return fuchsia_driver_framework::wire::NodeProperty::Builder(arena)
      .key(fuchsia_driver_framework::wire::NodePropertyKey::WithStringValue(arena, key))
      .value(fuchsia_driver_framework::wire::NodePropertyValue::WithEnumValue(arena, value))
      .Build();
}

}  // namespace driver

#endif  // LIB_DRIVER_COMPONENT_CPP_NODE_ADD_ARGS_H_
