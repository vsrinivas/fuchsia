// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYSMEM_MAKE_TRACKING_MAKE_TRACKING_IMPL_H_
#define LIB_SYSMEM_MAKE_TRACKING_MAKE_TRACKING_IMPL_H_

#include <lib/fidl/llcpp/allocator.h>
#include <lib/fidl/llcpp/traits.h>

namespace sysmem {
namespace internal {

template <typename T>
struct IsUnboundedArray : std::false_type {};
template <typename T>
struct IsUnboundedArray<T[]> : std::true_type {};

// Avoid allowing types we don't intend to allow.
template <typename T,
          typename TClean = typename std::remove_reference<typename std::remove_cv<T>::type>::type>
struct IsMakeableNonArray
    : std::integral_constant<bool,
                             // Just to be clear, not for arrays.
                             !std::is_array<T>::value && !std::is_volatile<T>::value &&
                                 !std::is_const<T>::value && fidl::IsFidlType<T>::value &&
                                 !fidl::IsStringView<TClean>::value &&
                                 // handled separately below
                                 !fidl::IsVectorView<TClean>::value> {};

template <typename Enable, typename T, typename... Args>
struct MakeTrackingImpl;

// tracking_ptr<T> MakeTracking<T>()
template <typename T>
struct MakeTrackingImpl<
    typename std::enable_if<IsMakeableNonArray<typename std::remove_reference<T>::type>::value &&
                            // Don't default-construct a table, since then we've got a
                            // tracking_ptr<Table> to a table without a Frame, which is almost never
                            // what we want.  We handle Table(s) separately below.
                            !fidl::IsTable<T>::value &&
                            // For cleaner error messages, this isn't for builders, which can't be
                            // default constructed anyway, so don't try for Builders.  Use
                            // allocator->make_table_builder<Table>() instead.
                            !fidl::IsTableBuilder<T>::value &&
                            // Use allocator->make_vec_ptr<T>(...) instead, or
                            // allocator->make_vec<>() then sysmem::MakeTracking(vector_view).
                            !fidl::IsVectorView<T>::value>::type,
    T> {
 private:
  using TNoRef = typename std::remove_reference<T>::type;
  using MutableT = typename std::remove_const<TNoRef>::type;

 public:
  static fidl::tracking_ptr<MutableT> MakeTrackingImplFunc(fidl::Allocator* allocator) {
    return allocator->make<MutableT>();
  }
};

// tracking_ptr<Table> MakeTracking<Table>()
//
// This handles MakeTracking<Table>() separately (vs above).  MakeTracking<Table>() creates an empty
// Table that has a Frame, which is useful for incremental building of a table field of a builder.
// In contrast, default construction of a Table and allocator->make<Table>() each create an empty
// table that does not have a Frame (so far).
template <typename Table>
struct MakeTrackingImpl<typename std::enable_if<fidl::IsTable<
                            typename std::remove_reference<Table>::type>::value>::type,
                        Table> {
 private:
  using TableNoRef = typename std::remove_reference<Table>::type;
  using MutableTable = typename std::remove_const<TableNoRef>::type;

 public:
  static fidl::tracking_ptr<MutableTable> MakeTrackingImplFunc(fidl::Allocator* allocator) {
    // This results in a tracking_ptr<> to a Table with a Frame set, where Table.IsEmpty().
    return allocator->make<MutableTable>(allocator->make_table_builder<MutableTable>().build());
  }
};

// tracking_ptr<T> MakeTracking<T>(t)
template <typename T>
struct MakeTrackingImpl<
    typename std::enable_if<!std::is_reference<T>::value &&
                            IsMakeableNonArray<typename std::remove_const<T>::type>::value &&
                            !fidl::IsTable<typename std::remove_const<T>::type>::value>::type,
    T, T> {
 private:
  using TNoC = typename std::remove_const<T>::type;

 public:
  static fidl::tracking_ptr<TNoC> MakeTrackingImplFunc(fidl::Allocator* allocator, TNoC arg) {
    return allocator->make<TNoC>(std::move(arg));
  }
};

// tracking_ptr<T> MakeTracking(t)
template <typename T>
struct MakeTrackingImpl<
    typename std::enable_if<!std::is_reference<T>::value &&
                            IsMakeableNonArray<typename std::remove_const<T>::type>::value &&
                            !fidl::IsTable<typename std::remove_const<T>::type>::value>::type,
    void, T> : MakeTrackingImpl<void, T, T> {};

// tracking_ptr<T> MakeTracking<T>(t&)
template <typename T>
struct MakeTrackingImpl<
    typename std::enable_if<IsMakeableNonArray<typename std::remove_const<T>::type>::value &&
                            !fidl::IsTable<typename std::remove_const<T>::type>::value>::type,
    T, T&> {
 private:
  using TNoC = typename std::remove_const<T>::type;

 public:
  static fidl::tracking_ptr<TNoC> MakeTrackingImplFunc(fidl::Allocator* allocator,
                                                       const TNoC& arg) {
    return allocator->make<TNoC>(arg);
  }
};

// tracking_ptr<T> MakeTracking(t&)
template <typename T>
struct MakeTrackingImpl<
    typename std::enable_if<IsMakeableNonArray<typename std::remove_const<T>::type>::value &&
                            !fidl::IsTable<typename std::remove_const<T>::type>::value>::type,
    void, T&> : MakeTrackingImpl<void, T, T&> {};

// tracking_ptr<Table> MakeTracking<Table>(table)
template <typename Table>
struct MakeTrackingImpl<typename std::enable_if<fidl::IsTable<
                            typename std::remove_reference<Table>::type>::value>::type,
                        Table, Table> {
 private:
  using TableNoRef = typename std::remove_reference<Table>::type;
  using MutableTable = typename std::remove_const<TableNoRef>::type;

 public:
  static fidl::tracking_ptr<MutableTable> MakeTrackingImplFunc(fidl::Allocator* allocator,
                                                               TableNoRef table) {
    return allocator->make<MutableTable>(std::forward<TableNoRef>(table));
  }
};

// tracking_ptr<Table> MakeTracking(table)
template <typename Table>
struct MakeTrackingImpl<typename std::enable_if<fidl::IsTable<
                            typename std::remove_reference<Table>::type>::value>::type,
                        void, Table> : MakeTrackingImpl<void, Table, Table> {};

// tracking_ptr<Table> MakeTracking<Table>(builder)
template <typename Table>
struct MakeTrackingImpl<
    typename std::enable_if<IsMakeableNonArray<Table>::value && fidl::IsTable<Table>::value>::type,
    Table, typename Table::Builder> {
 private:
  static_assert(!std::is_reference<Table>::value);
  static_assert(!std::is_const<Table>::value);
  static_assert(!std::is_volatile<Table>::value);
  static_assert(!std::is_pointer<Table>::value);

 public:
  static fidl::tracking_ptr<Table> MakeTrackingImplFunc(fidl::Allocator* allocator,
                                                        typename Table::Builder builder) {
    return MakeTrackingImpl<void, Table, Table>::MakeTrackingImplFunc(allocator, builder.build());
  }
};

// tracking_ptr<Table> MakeTracking(builder)
template <typename TableBuilder>
struct MakeTrackingImpl<
    typename std::enable_if<
        IsMakeableNonArray<decltype(std::declval<TableBuilder>().build())>::value &&
        fidl::IsTable<decltype(std::declval<TableBuilder>().build())>::value>::type,
    void, TableBuilder>
    : MakeTrackingImpl<void, decltype(std::declval<TableBuilder>().build()), TableBuilder> {};

// tracking_ptr<VectorView<T>> MakeTracking<VectorView<T>>(vector_view)
template <typename T>
struct MakeTrackingImpl<typename std::enable_if<IsMakeableNonArray<
                            typename std::remove_reference<T>::type>::value>::type,
                        fidl::VectorView<T>, fidl::VectorView<T>> {
  static fidl::tracking_ptr<fidl::VectorView<T>> MakeTrackingImplFunc(
      fidl::Allocator* allocator, fidl::VectorView<T>&& vector_view) {
    return allocator->make<fidl::VectorView<T>>(std::move(vector_view));
  }
};

// tracking_ptr<VectorView<T>> MakeTracking(vector_view)
template <typename T>
struct MakeTrackingImpl<typename std::enable_if<IsMakeableNonArray<
                            typename std::remove_reference<T>::type>::value>::type,
                        void, fidl::VectorView<T>>
    : MakeTrackingImpl<void, fidl::VectorView<T>, fidl::VectorView<T>> {};

// The error message when forgetting std::move() on the VectorView<> argument is otherwise not very
// helpful.
template <typename T>
struct MakeTrackingImpl<typename std::enable_if<IsMakeableNonArray<
                            typename std::remove_reference<T>::type>::value>::type,
                        fidl::VectorView<T>, fidl::VectorView<T>&> {
  static_assert(!std::is_reference<fidl::VectorView<T>&>::value,
                "Use std::move() on the fidl::VectorView<T> argument");
};

// The error message when forgetting std::move() on the VectorView<> argument is otherwise not very
// helpful.
template <typename T>
struct MakeTrackingImpl<typename std::enable_if<IsMakeableNonArray<
                            typename std::remove_reference<T>::type>::value>::type,
                        void, fidl::VectorView<T>&> {
  static_assert(!std::is_reference<fidl::VectorView<T>&>::value,
                "Use std::move() on the fidl::VectorView<T> argument");
};

}  // namespace internal
}  // namespace sysmem

#endif  // LIB_SYSMEM_MAKE_TRACKING_MAKE_TRACKING_IMPL_H_
