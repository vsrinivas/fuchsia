// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_FUCHSIA_INTL_EXT_CPP_FIDL_EXT_H_
#define SRC_LIB_FIDL_FUCHSIA_INTL_EXT_CPP_FIDL_EXT_H_

#include <type_traits>

namespace fuchsia {
namespace intl {

namespace {
template <typename T>
struct HasFidlEquals : public std::false_type {};

// clang-format off

template <> struct HasFidlEquals<fuchsia::intl::CalendarId> : public std::true_type {};
template <> struct HasFidlEquals<fuchsia::intl::LocaleId> : public std::true_type {};
template <> struct HasFidlEquals<fuchsia::intl::Profile> : public std::true_type {};
template <> struct HasFidlEquals<fuchsia::intl::RegulatoryDomain> : public std::true_type {};
template <> struct HasFidlEquals<fuchsia::intl::TimeZoneId> : public std::true_type {};

// clang-format on

}  // namespace

// Implement operator== for types listed above.
template <typename T, typename = std::enable_if_t<HasFidlEquals<T>::value>>
bool operator==(const T& a, const T& b) {
  return fidl::Equals(a, b);
}

// Implement operator== for types listed above.
template <typename T, typename = std::enable_if_t<HasFidlEquals<T>::value>>
bool operator!=(const T& a, const T& b) {
  return !(a == b);
}

}  // namespace intl
}  // namespace fuchsia

#endif  // SRC_LIB_FIDL_FUCHSIA_INTL_EXT_CPP_FIDL_EXT_H_
