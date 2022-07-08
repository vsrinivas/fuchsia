// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_FORMATTING_MACROS_H_
#define SRC_CAMERA_LIB_FORMATTING_MACROS_H_

// NOLINTBEGIN(bugprone-macro-parentheses): argument is type, not value
// clang-format off

#define COMMON_BEGIN(ns, type) \
  namespace ns { \
  using camera::formatting::Dump; \
  camera::formatting::PropertyListPtr Dump(const ns::type& x); \
  template <> \
  std::ostream& operator<<(std::ostream& os, const ns::type& x) { \
    os << Dump(x); \
    return os; \
  } \
  camera::formatting::PropertyListPtr Dump(const ns::type& x) { \
    using ns::type; \
    auto p = camera::formatting::PropertyList::New(); \
    auto& properties = p->properties;

#define COMMON_END() \
    return p; \
  } \
  }

#define ENUM_BEGIN(ns, type) \
  COMMON_BEGIN(ns, type) \
  std::string s = [&] { \
    switch (x) {

#define ENUM_ELEMENT(v) \
      case (v): return #v;

#define ENUM_END() \
    } \
  }(); \
  properties.push_back({.value = std::move(s)}); \
  COMMON_END()

#define BITS_BEGIN(ns, type) \
  COMMON_BEGIN(ns, type) \

#define BITS_ELEMENT(v) \
  if (static_cast<uint64_t>(x) & static_cast<uint64_t>(v)) { \
    properties.push_back({.value = #v}); \
  }

#define BITS_END() \
  COMMON_END()

#define TABLE_BEGIN(ns, type) \
  COMMON_BEGIN(ns, type)

#define TABLE_ELEMENT(v) \
  if (x.has_##v()) { \
    properties.push_back({.name = #v, .value = Dump(x.v())}); \
  } else { \
    properties.push_back({.name = #v, .value = "<unset>"}); \
  }

#define TABLE_END() \
  COMMON_END()

#define STRUCT_BEGIN(ns, type) \
  COMMON_BEGIN(ns, type)

#define STRUCT_ELEMENT(v) \
  properties.push_back({.name = #v, .value = Dump(x.v)});

#define STRUCT_END() \
  COMMON_END()

#define MARK_NOTIMPL(ns, type) \
  COMMON_BEGIN(ns, type) \
  properties.push_back({.value = "<logging for '" #type "' not implemented>"}); \
  COMMON_END()

// clang-format on
// NOLINTEND(bugprone-macro-parentheses)

#endif  // SRC_CAMERA_LIB_FORMATTING_MACROS_H_
