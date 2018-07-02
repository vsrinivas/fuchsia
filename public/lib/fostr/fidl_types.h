// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FOSTR_FIDL_TYPES_H_
#define LIB_FOSTR_FIDL_TYPES_H_

#include "lib/fidl/cpp/array.h"
#include "lib/fidl/cpp/vector.h"
#include "lib/fostr/indent.h"

namespace fostr {
namespace internal {

template <typename Iter>
void insert_sequence_container(std::ostream& os, Iter begin, Iter end) {
  if (begin == end) {
    os << "<empty>";
  }

  int index = 0;
  for (; begin != end; ++begin) {
    os << NewLine << "[" << index++ << "] " << *begin;
  }
}

}  // namespace internal
}  // namespace fostr

namespace fidl {

// ostream operator<< templates for fidl types. These templates conform to the
// convention described in indent.h.

template <typename T, size_t N>
std::ostream& operator<<(std::ostream& os, const Array<T, N>& value) {
  fostr::internal::insert_sequence_container(os, value.cbegin(), value.cend());
  return os;
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const VectorPtr<T>& value) {
  fostr::internal::insert_sequence_container(os, value.get().cbegin(),
                                             value.get().cend());
  return os;
}

// TODO(dalesat): zircon_types.h
// TODO(dalesat): Binding
// TODO(dalesat): BindingSet
// TODO(dalesat): InterfaceHandle
// TODO(dalesat): InterfacePtr
// TODO(dalesat): InterfaceRequest

}  // namespace fidl

#endif  // LIB_FOSTR_FIDL_TYPES_H_
