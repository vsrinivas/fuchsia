// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYSMEM_MAKE_TRACKING_MAKE_TRACKING_H_
#define LIB_SYSMEM_MAKE_TRACKING_MAKE_TRACKING_H_

#include "make_tracking_impl.h"

// This lib is under sysmem instead of fidl for now.  For it to be (mostly) under fidl, we'll
// probably want to infer the T in fidl::Allocator::make<T>() instead of having a separate
// MakeTracking<>().
namespace sysmem {

// MakeTracking<>() is convenient for setting fields of a Table::Builder.  When setting a field to a
// value of the same type, the type of the field doesn't need to be specified.
//
// When creating a logically new Table (not moved from an existing Table), the Table::Frame will be
// present (in contrast to allocator->make<Table>() which results in a Table with no Table::Frame,
// which is fine for a Table that'll remain empty).  Having a Table::Frame is useful for
// incrementally setting fields of a table sub-field of a builder by using get_builder_field().
//
// Usage (setting a field_value of same type as field):
// table_builder.set_field(sysmem::MakeTracking(&allocator, field_value));
// or
// table_builder.set_field(sysmem::MakeTracking(&allocator, std::move(field_value)));
//
// Usage (setting an empty table with full-size Table::Frame):
// table_builder.set_table_field(sysmem::MakeTracking<Table>(&allocator));
//
// Usage (setting a table field using a builder which is auto-built):
// table_builder.set_table_field(sysmem::MakeTracking(&allocator, std::move(field_table_builder)));
//
// Usage (setting a VectorView into a table field that needs a tracking_ptr<VectorView<T>>):
// table_builder.set_vector_field(sysmem::MakeTracking(&allocator, std::move(vector_view)));
//
// If explicitly specifying the type is desired (and not making an empty table that needs a Frame),
// consider fidl::Allocator::make<T>(...).
//
// If making a VectorView, see fidl::Allocator::make_vec<T>(count, capacity).
//
// If making a tracking_ptr<VectorView>, see fidl::Allocator::make_vec_ptr<T>(count, capacity).
//
// If making a Table::Builder, see fidl::Allocator::make_table_builder<Table>().
template <typename T = void, typename... Args>
auto MakeTracking(fidl::Allocator* allocator, Args&&... args) {
  return internal::MakeTrackingImpl<void, T, Args...>::MakeTrackingImplFunc(
      allocator, std::forward<Args>(args)...);
}

}  // namespace sysmem

#endif  // LIB_SYSMEM_MAKE_TRACKING_MAKE_TRACKING_H_
